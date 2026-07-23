/*
Copyright (C) 2010 Andrey Nazarov

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

#include "sound.h"
#include "qal.h"
#include "common/json.h"
#include <cfloat>

// translates from AL coordinate system to quake
#define AL_UnpackVector(v)  -v[1],v[2],-v[0]
#define AL_CopyVector(a,b)  ((b)[0]=-(a)[1],(b)[1]=(a)[2],(b)[2]=-(a)[0])

// OpenAL implementation should support at least this number of sources
#define MIN_CHANNELS    16

// Quake units per second (1 unit ~= 1 inch).
static constexpr float AL_DOPPLER_SPEED = 13500.0f;
static constexpr float AL_DOPPLER_TELEPORT_DISTANCE = 1024.0f;

static cvar_t       *al_reverb;
static cvar_t       *al_reverb_lerp_time;
static cvar_t       *al_reverb_send;
static cvar_t       *al_reverb_send_distance;
static cvar_t       *al_reverb_send_min;
static cvar_t       *al_reverb_send_occlusion_boost;
static cvar_t       *al_eax;
static cvar_t       *al_eax_lerp_time;

static cvar_t       *al_timescale;
static cvar_t       *al_merge_looping;
static cvar_t       *al_distance_model;
static cvar_t       *al_air_absorption;
static cvar_t       *al_air_absorption_distance;
static cvar_t       *al_doppler;
static cvar_t       *al_doppler_speed;
static cvar_t       *al_doppler_min_speed;
static cvar_t       *al_doppler_max_speed;
static cvar_t       *al_doppler_smooth;

static ALuint       *s_srcnums;
static int          s_numalsources;
static ALuint       s_stream;
static ALuint       s_stream_buffers;
static bool         s_stream_paused;
static bool         s_loop_points;
static bool         s_source_spatialize;
static ALuint       s_framecount;
static ALint        s_merge_looping_minval;

static ALuint       s_underwater_filter;
static bool         s_underwater_flag;
static ALuint       *s_occlusion_filters;
static int          s_num_occlusion_filters;
static ALuint       *s_reverb_filters;
static int          s_num_reverb_filters;
static ALenum       s_air_absorption_enum;
static bool         s_air_absorption_supported;

typedef struct {
    vec3_t  origin;
    vec3_t  velocity;
    int     time;
} doppler_state_t;

static doppler_state_t   s_doppler_state[MAX_EDICTS];
static vec3_t            s_doppler_listener_velocity;

static float AL_GetLoopSoundPhaseOffsetSeconds(const channel_t *ch, const sfxcache_t *sc);

// reverb stuff
typedef struct {
    char    material[16];
    int16_t step_id;
} al_reverb_material_t;

typedef struct {
    al_reverb_material_t    *materials; // if null, matches everything
    size_t                  num_materials;
    uint8_t                 preset;
} al_reverb_entry_t;

typedef struct {
    float               dimension;
    al_reverb_entry_t   *reverbs;
    size_t              num_reverbs;
} al_reverb_environment_t;

typedef enum {
    AL_REVERB_DRIVER_NONE,
    AL_REVERB_DRIVER_AUTO,
    AL_REVERB_DRIVER_EAX
} al_reverb_driver_t;

static size_t                   s_num_reverb_environments;
static al_reverb_environment_t  *s_reverb_environments;

static ALuint       s_reverb_effect;
static ALuint       s_reverb_slot;
static bool         s_eax_effect_available;

typedef struct {
    vec3_t      origin;
    float       radius;
    qhandle_t   reverb_id;
} al_eax_zone_t;

static al_eax_zone_t            *s_eax_zones;
static size_t                   s_num_eax_zones;
static sfx_eax_properties_t     s_eax_effects[SOUND_EAX_EFFECT_MAX];
static qhandle_t                s_eax_current_id;
static qhandle_t                s_eax_previous_id;
static float                    s_eax_lerp_fraction;
static sfx_eax_properties_t     s_eax_mixed_properties;

static const EFXEAXREVERBPROPERTIES s_reverb_parameters[] = {
    EFX_REVERB_PRESET_GENERIC,
    EFX_REVERB_PRESET_PADDEDCELL,
    EFX_REVERB_PRESET_ROOM,
    EFX_REVERB_PRESET_BATHROOM,
    EFX_REVERB_PRESET_LIVINGROOM,
    EFX_REVERB_PRESET_STONEROOM,
    EFX_REVERB_PRESET_AUDITORIUM,
    EFX_REVERB_PRESET_CONCERTHALL,
    EFX_REVERB_PRESET_CAVE,
    EFX_REVERB_PRESET_ARENA,
    EFX_REVERB_PRESET_HANGAR,
    EFX_REVERB_PRESET_CARPETEDHALLWAY,
    EFX_REVERB_PRESET_HALLWAY,
    EFX_REVERB_PRESET_STONECORRIDOR,
    EFX_REVERB_PRESET_ALLEY,
    EFX_REVERB_PRESET_FOREST,
    EFX_REVERB_PRESET_CITY,
    EFX_REVERB_PRESET_MOUNTAINS,
    EFX_REVERB_PRESET_QUARRY,
    EFX_REVERB_PRESET_PLAIN,
    EFX_REVERB_PRESET_PARKINGLOT,
    EFX_REVERB_PRESET_SEWERPIPE,
    EFX_REVERB_PRESET_UNDERWATER,
    EFX_REVERB_PRESET_DRUGGED,
    EFX_REVERB_PRESET_DIZZY,
    EFX_REVERB_PRESET_PSYCHOTIC
};

static EFXEAXREVERBPROPERTIES   s_active_reverb;
static EFXEAXREVERBPROPERTIES   s_reverb_lerp_to, s_reverb_lerp_result;
static int                      s_reverb_lerp_start, s_reverb_lerp_time;
static uint8_t                  s_reverb_current_preset;
static al_reverb_driver_t       s_reverb_driver;

static const char *const s_reverb_names[] = {
    "generic",
    "padded_cell",
    "room",
    "bathroom",
    "living_room",
    "stone_room",
    "auditorium",
    "concert_hall",
    "cave",
    "arena",
    "hangar",
    "carpeted_hallway",
    "hallway",
    "stone_corridor",
    "alley",
    "forest",
    "city",
    "mountains",
    "quarry",
    "plain",
    "parking_lot",
    "sewer_pipe",
    "underwater",
    "drugged",
    "dizzy",
    "psychotic"
};

static const qboolean AL_SetEAXEffectProperties(const sfx_eax_properties_t *reverb);

// EAX environment pipeline ported from Q2RTXPerimental's client EAX system
// (PolyhedronStudio), with adaptation to WORR's OpenAL backend interfaces.
static const sfx_eax_properties_t s_eax_default_properties = {
    .flDensity = 1.0f,
    .flDiffusion = 1.0f,
    .flGain = 0.0f,
    .flGainHF = 1.0f,
    .flGainLF = 1.0f,
    .flDecayTime = 1.0f,
    .flDecayHFRatio = 1.0f,
    .flDecayLFRatio = 1.0f,
    .flReflectionsGain = 0.0f,
    .flReflectionsDelay = 0.0f,
    .flReflectionsPan = { 0.0f, 0.0f, 0.0f },
    .flLateReverbGain = 1.0f,
    .flLateReverbDelay = 0.0f,
    .flLateReverbPan = { 0.0f, 0.0f, 0.0f },
    .flEchoTime = 0.25f,
    .flEchoDepth = 0.0f,
    .flModulationTime = 0.25f,
    .flModulationDepth = 0.0f,
    .flAirAbsorptionGainHF = 1.0f,
    .flHFReference = 5000.0f,
    .flLFReference = 250.0f,
    .flRoomRolloffFactor = 0.0f,
    .iDecayHFLimit = 1
};

static const sfx_eax_properties_t s_eax_underwater_properties = {
    .flDensity = 0.3645f,
    .flDiffusion = 1.0f,
    .flGain = 0.3162f,
    .flGainHF = 0.01f,
    .flGainLF = 1.0f,
    .flDecayTime = 1.49f,
    .flDecayHFRatio = 0.1f,
    .flDecayLFRatio = 1.0f,
    .flReflectionsGain = 0.5963f,
    .flReflectionsDelay = 0.007f,
    .flReflectionsPan = { 0.0f, 0.0f, 0.0f },
    .flLateReverbGain = 7.0795f,
    .flLateReverbDelay = 0.011f,
    .flLateReverbPan = { 0.0f, 0.0f, 0.0f },
    .flEchoTime = 0.25f,
    .flEchoDepth = 0.0f,
    .flModulationTime = 1.18f,
    .flModulationDepth = 0.348f,
    .flAirAbsorptionGainHF = 0.9943f,
    .flHFReference = 5000.0f,
    .flLFReference = 250.0f,
    .flRoomRolloffFactor = 0.0f,
    .iDecayHFLimit = 1
};

static const char *const s_eax_json_names[SOUND_EAX_EFFECT_MAX] = {
    nullptr, nullptr,
    "abandoned",
    "alley",
    "arena",
    "auditorium",
    "bathroom",
    "carpetedhallway",
    "cave",
    "chapel",
    "city",
    "citystreets",
    "concerthall",
    "dizzy",
    "drugged",
    "dustyroom",
    "forest",
    "hallway",
    "hangar",
    "library",
    "livingroom",
    "mountains",
    "museum",
    "paddedcell",
    "parkinglot",
    "plain",
    "psychotic",
    "quarry",
    "room",
    "sewerpipe",
    "smallwaterroom",
    "stonecorridor",
    "stoneroom",
    "subway",
    "underpass"
};

static void AL_LoadEffect(const EFXEAXREVERBPROPERTIES *reverb)
{
    sfx_eax_properties_t properties{};

    properties.flDensity = reverb->flDensity;
    properties.flDiffusion = reverb->flDiffusion;
    properties.flGain = reverb->flGain;
    properties.flGainHF = reverb->flGainHF;
    properties.flGainLF = reverb->flGainLF;
    properties.flDecayTime = reverb->flDecayTime;
    properties.flDecayHFRatio = reverb->flDecayHFRatio;
    properties.flDecayLFRatio = reverb->flDecayLFRatio;
    properties.flReflectionsGain = reverb->flReflectionsGain;
    properties.flReflectionsDelay = reverb->flReflectionsDelay;
    properties.flReflectionsPan[0] = reverb->flReflectionsPan[0];
    properties.flReflectionsPan[1] = reverb->flReflectionsPan[1];
    properties.flReflectionsPan[2] = reverb->flReflectionsPan[2];
    properties.flLateReverbGain = reverb->flLateReverbGain;
    properties.flLateReverbDelay = reverb->flLateReverbDelay;
    properties.flLateReverbPan[0] = reverb->flLateReverbPan[0];
    properties.flLateReverbPan[1] = reverb->flLateReverbPan[1];
    properties.flLateReverbPan[2] = reverb->flLateReverbPan[2];
    properties.flEchoTime = reverb->flEchoTime;
    properties.flEchoDepth = reverb->flEchoDepth;
    properties.flModulationTime = reverb->flModulationTime;
    properties.flModulationDepth = reverb->flModulationDepth;
    properties.flAirAbsorptionGainHF = reverb->flAirAbsorptionGainHF;
    properties.flHFReference = reverb->flHFReference;
    properties.flLFReference = reverb->flLFReference;
    properties.flRoomRolloffFactor = reverb->flRoomRolloffFactor;
    properties.iDecayHFLimit = reverb->iDecayHFLimit;

    AL_SetEAXEffectProperties(&properties);
}

static const qboolean AL_SetEAXEffectProperties(const sfx_eax_properties_t *reverb)
{
    if (!reverb || !s_reverb_effect || !s_reverb_slot)
        return qfalse;

    // Clear stale AL error state so the return value reflects this update call.
    qalGetError();

    ALuint effect = s_reverb_effect;

    if (s_eax_effect_available) {
        qalEffecti(effect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);

        qalEffectf(effect, AL_EAXREVERB_DENSITY, Q_clipf(reverb->flDensity, AL_EAXREVERB_MIN_DENSITY, AL_EAXREVERB_MAX_DENSITY));
        qalEffectf(effect, AL_EAXREVERB_DIFFUSION, Q_clipf(reverb->flDiffusion, AL_EAXREVERB_MIN_DIFFUSION, AL_EAXREVERB_MAX_DIFFUSION));
        qalEffectf(effect, AL_EAXREVERB_GAIN, Q_clipf(reverb->flGain, AL_EAXREVERB_MIN_GAIN, AL_EAXREVERB_MAX_GAIN));
        qalEffectf(effect, AL_EAXREVERB_GAINHF, Q_clipf(reverb->flGainHF, AL_EAXREVERB_MIN_GAINHF, AL_EAXREVERB_MAX_GAINHF));
        qalEffectf(effect, AL_EAXREVERB_GAINLF, Q_clipf(reverb->flGainLF, AL_EAXREVERB_MIN_GAINLF, AL_EAXREVERB_MAX_GAINLF));
        qalEffectf(effect, AL_EAXREVERB_DECAY_TIME, Q_clipf(reverb->flDecayTime, AL_EAXREVERB_MIN_DECAY_TIME, AL_EAXREVERB_MAX_DECAY_TIME));
        qalEffectf(effect, AL_EAXREVERB_DECAY_HFRATIO, Q_clipf(reverb->flDecayHFRatio, AL_EAXREVERB_MIN_DECAY_HFRATIO, AL_EAXREVERB_MAX_DECAY_HFRATIO));
        qalEffectf(effect, AL_EAXREVERB_DECAY_LFRATIO, Q_clipf(reverb->flDecayLFRatio, AL_EAXREVERB_MIN_DECAY_LFRATIO, AL_EAXREVERB_MAX_DECAY_LFRATIO));
        qalEffectf(effect, AL_EAXREVERB_REFLECTIONS_GAIN, Q_clipf(reverb->flReflectionsGain, AL_EAXREVERB_MIN_REFLECTIONS_GAIN, AL_EAXREVERB_MAX_REFLECTIONS_GAIN));
        qalEffectf(effect, AL_EAXREVERB_REFLECTIONS_DELAY, Q_clipf(reverb->flReflectionsDelay, AL_EAXREVERB_MIN_REFLECTIONS_DELAY, AL_EAXREVERB_MAX_REFLECTIONS_DELAY));
        qalEffectfv(effect, AL_EAXREVERB_REFLECTIONS_PAN, reverb->flReflectionsPan);
        qalEffectf(effect, AL_EAXREVERB_LATE_REVERB_GAIN, Q_clipf(reverb->flLateReverbGain, AL_EAXREVERB_MIN_LATE_REVERB_GAIN, AL_EAXREVERB_MAX_LATE_REVERB_GAIN));
        qalEffectf(effect, AL_EAXREVERB_LATE_REVERB_DELAY, Q_clipf(reverb->flLateReverbDelay, AL_EAXREVERB_MIN_LATE_REVERB_DELAY, AL_EAXREVERB_MAX_LATE_REVERB_DELAY));
        qalEffectfv(effect, AL_EAXREVERB_LATE_REVERB_PAN, reverb->flLateReverbPan);
        qalEffectf(effect, AL_EAXREVERB_ECHO_TIME, Q_clipf(reverb->flEchoTime, AL_EAXREVERB_MIN_ECHO_TIME, AL_EAXREVERB_MAX_ECHO_TIME));
        qalEffectf(effect, AL_EAXREVERB_ECHO_DEPTH, Q_clipf(reverb->flEchoDepth, AL_EAXREVERB_MIN_ECHO_DEPTH, AL_EAXREVERB_MAX_ECHO_DEPTH));
        qalEffectf(effect, AL_EAXREVERB_MODULATION_TIME, Q_clipf(reverb->flModulationTime, AL_EAXREVERB_MIN_MODULATION_TIME, AL_EAXREVERB_MAX_MODULATION_TIME));
        qalEffectf(effect, AL_EAXREVERB_MODULATION_DEPTH, Q_clipf(reverb->flModulationDepth, AL_EAXREVERB_MIN_MODULATION_DEPTH, AL_EAXREVERB_MAX_MODULATION_DEPTH));
        qalEffectf(effect, AL_EAXREVERB_AIR_ABSORPTION_GAINHF, Q_clipf(reverb->flAirAbsorptionGainHF, AL_EAXREVERB_MIN_AIR_ABSORPTION_GAINHF, AL_EAXREVERB_MAX_AIR_ABSORPTION_GAINHF));
        qalEffectf(effect, AL_EAXREVERB_HFREFERENCE, Q_clipf(reverb->flHFReference, AL_EAXREVERB_MIN_HFREFERENCE, AL_EAXREVERB_MAX_HFREFERENCE));
        qalEffectf(effect, AL_EAXREVERB_LFREFERENCE, Q_clipf(reverb->flLFReference, AL_EAXREVERB_MIN_LFREFERENCE, AL_EAXREVERB_MAX_LFREFERENCE));
        qalEffectf(effect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, Q_clipf(reverb->flRoomRolloffFactor, AL_EAXREVERB_MIN_ROOM_ROLLOFF_FACTOR, AL_EAXREVERB_MAX_ROOM_ROLLOFF_FACTOR));
        qalEffecti(effect, AL_EAXREVERB_DECAY_HFLIMIT, Q_clip(reverb->iDecayHFLimit, AL_EAXREVERB_MIN_DECAY_HFLIMIT, AL_EAXREVERB_MAX_DECAY_HFLIMIT));
    } else {
        qalEffecti(effect, AL_EFFECT_TYPE, AL_EFFECT_REVERB);

        qalEffectf(effect, AL_REVERB_DENSITY, Q_clipf(reverb->flDensity, AL_REVERB_MIN_DENSITY, AL_REVERB_MAX_DENSITY));
        qalEffectf(effect, AL_REVERB_DIFFUSION, Q_clipf(reverb->flDiffusion, AL_REVERB_MIN_DIFFUSION, AL_REVERB_MAX_DIFFUSION));
        qalEffectf(effect, AL_REVERB_GAIN, Q_clipf(reverb->flGain, AL_REVERB_MIN_GAIN, AL_REVERB_MAX_GAIN));
        qalEffectf(effect, AL_REVERB_GAINHF, Q_clipf(reverb->flGainHF, AL_REVERB_MIN_GAINHF, AL_REVERB_MAX_GAINHF));
        qalEffectf(effect, AL_REVERB_DECAY_TIME, Q_clipf(reverb->flDecayTime, AL_REVERB_MIN_DECAY_TIME, AL_REVERB_MAX_DECAY_TIME));
        qalEffectf(effect, AL_REVERB_DECAY_HFRATIO, Q_clipf(reverb->flDecayHFRatio, AL_REVERB_MIN_DECAY_HFRATIO, AL_REVERB_MAX_DECAY_HFRATIO));
        qalEffectf(effect, AL_REVERB_REFLECTIONS_GAIN, Q_clipf(reverb->flReflectionsGain, AL_REVERB_MIN_REFLECTIONS_GAIN, AL_REVERB_MAX_REFLECTIONS_GAIN));
        qalEffectf(effect, AL_REVERB_REFLECTIONS_DELAY, Q_clipf(reverb->flReflectionsDelay, AL_REVERB_MIN_REFLECTIONS_DELAY, AL_REVERB_MAX_REFLECTIONS_DELAY));
        qalEffectf(effect, AL_REVERB_LATE_REVERB_GAIN, Q_clipf(reverb->flLateReverbGain, AL_REVERB_MIN_LATE_REVERB_GAIN, AL_REVERB_MAX_LATE_REVERB_GAIN));
        qalEffectf(effect, AL_REVERB_LATE_REVERB_DELAY, Q_clipf(reverb->flLateReverbDelay, AL_REVERB_MIN_LATE_REVERB_DELAY, AL_REVERB_MAX_LATE_REVERB_DELAY));
        qalEffectf(effect, AL_REVERB_AIR_ABSORPTION_GAINHF, Q_clipf(reverb->flAirAbsorptionGainHF, AL_REVERB_MIN_AIR_ABSORPTION_GAINHF, AL_REVERB_MAX_AIR_ABSORPTION_GAINHF));
        qalEffectf(effect, AL_REVERB_ROOM_ROLLOFF_FACTOR, Q_clipf(reverb->flRoomRolloffFactor, AL_REVERB_MIN_ROOM_ROLLOFF_FACTOR, AL_REVERB_MAX_ROOM_ROLLOFF_FACTOR));
        qalEffecti(effect, AL_REVERB_DECAY_HFLIMIT, Q_clip(reverb->iDecayHFLimit, AL_REVERB_MIN_DECAY_HFLIMIT, AL_REVERB_MAX_DECAY_HFLIMIT));
    }

    qalAuxiliaryEffectSloti(s_reverb_slot, AL_EFFECTSLOT_EFFECT, effect);
    return (qalGetError() == AL_NO_ERROR) ? qtrue : qfalse;
}

static float AL_ParseJsonFloat(json_parse_t *parser)
{
    char buffer[64];
    Json_Ensure(parser, JSMN_PRIMITIVE);
    Q_strnlcpy(buffer, parser->buffer + parser->pos->start,
               parser->pos->end - parser->pos->start, sizeof(buffer));
    parser->pos++;
    return (float)atof(buffer);
}

static int AL_ParseJsonInt(json_parse_t *parser)
{
    char buffer[32];
    Json_Ensure(parser, JSMN_PRIMITIVE);
    Q_strnlcpy(buffer, parser->buffer + parser->pos->start,
               parser->pos->end - parser->pos->start, sizeof(buffer));
    parser->pos++;
    return atoi(buffer);
}

static bool AL_ParseJsonBool(json_parse_t *parser, bool default_value)
{
    char buffer[32];
    Json_Ensure(parser, JSMN_PRIMITIVE);
    Q_strnlcpy(buffer, parser->buffer + parser->pos->start,
               parser->pos->end - parser->pos->start, sizeof(buffer));
    parser->pos++;

    if (!Q_strcasecmp(buffer, "true"))
        return true;
    if (!Q_strcasecmp(buffer, "false"))
        return false;
    if (buffer[0])
        return atoi(buffer) != 0;

    return default_value;
}

static void AL_ParseJsonString(json_parse_t *parser, char *out, size_t out_size)
{
    Json_Ensure(parser, JSMN_STRING);
    Q_strnlcpy(out, parser->buffer + parser->pos->start,
               parser->pos->end - parser->pos->start, out_size);
    parser->pos++;
}

static void AL_ParseJsonVec3(json_parse_t *parser, float out[3])
{
    size_t array_size = Json_EnsureNext(parser, JSMN_ARRAY)->size;
    for (int i = 0; i < 3; i++) {
        if ((size_t)i < array_size) {
            out[i] = AL_ParseJsonFloat(parser);
        } else {
            out[i] = 0.0f;
        }
    }
    for (size_t i = 3; i < array_size; i++)
        Json_SkipToken(parser);
}

static sfx_eax_properties_t AL_LoadEAXPropertiesFromJSON(const char *name)
{
    sfx_eax_properties_t properties = s_eax_default_properties;
    char path[MAX_QPATH];
    json_parse_t parser{};

    Q_snprintf(path, sizeof(path), "eax/%s.json", name);

    if (Json_ErrorHandler(parser)) {
        if (parser.tokens || parser.buffer)
            Json_Free(&parser);
        Com_WPrintf("Couldn't load %s[%s]; %s\n", path, parser.error_loc, parser.error);
        return properties;
    }

    Json_Load(path, &parser);
    size_t fields = Json_EnsureNext(&parser, JSMN_OBJECT)->size;

    for (size_t i = 0; i < fields; i++) {
        if (!Json_Strcmp(&parser, "density")) {
            parser.pos++;
            properties.flDensity = AL_ParseJsonFloat(&parser);
        } else if (!Json_Strcmp(&parser, "diffusion")) {
            parser.pos++;
            properties.flDiffusion = AL_ParseJsonFloat(&parser);
        } else if (!Json_Strcmp(&parser, "gain")) {
            parser.pos++;
            properties.flGain = AL_ParseJsonFloat(&parser);
        } else if (!Json_Strcmp(&parser, "gain_hf")) {
            parser.pos++;
            properties.flGainHF = AL_ParseJsonFloat(&parser);
        } else if (!Json_Strcmp(&parser, "gain_lf")) {
            parser.pos++;
            properties.flGainLF = AL_ParseJsonFloat(&parser);
        } else if (!Json_Strcmp(&parser, "decay_time")) {
            parser.pos++;
            properties.flDecayTime = AL_ParseJsonFloat(&parser);
        } else if (!Json_Strcmp(&parser, "decay_hf_ratio")) {
            parser.pos++;
            properties.flDecayHFRatio = AL_ParseJsonFloat(&parser);
        } else if (!Json_Strcmp(&parser, "decay_lf_ratio")) {
            parser.pos++;
            properties.flDecayLFRatio = AL_ParseJsonFloat(&parser);
        } else if (!Json_Strcmp(&parser, "reflections_gain")) {
            parser.pos++;
            properties.flReflectionsGain = AL_ParseJsonFloat(&parser);
        } else if (!Json_Strcmp(&parser, "reflections_delay")) {
            parser.pos++;
            properties.flReflectionsDelay = AL_ParseJsonFloat(&parser);
        } else if (!Json_Strcmp(&parser, "reflections_pan")) {
            parser.pos++;
            AL_ParseJsonVec3(&parser, properties.flReflectionsPan);
        } else if (!Json_Strcmp(&parser, "late_reverb_gain")) {
            parser.pos++;
            properties.flLateReverbGain = AL_ParseJsonFloat(&parser);
        } else if (!Json_Strcmp(&parser, "late_reverb_delay")) {
            parser.pos++;
            properties.flLateReverbDelay = AL_ParseJsonFloat(&parser);
        } else if (!Json_Strcmp(&parser, "late_reverb_pan")) {
            parser.pos++;
            AL_ParseJsonVec3(&parser, properties.flLateReverbPan);
        } else if (!Json_Strcmp(&parser, "echo_time")) {
            parser.pos++;
            properties.flEchoTime = AL_ParseJsonFloat(&parser);
        } else if (!Json_Strcmp(&parser, "echo_depth")) {
            parser.pos++;
            properties.flEchoDepth = AL_ParseJsonFloat(&parser);
        } else if (!Json_Strcmp(&parser, "modulation_time")) {
            parser.pos++;
            properties.flModulationTime = AL_ParseJsonFloat(&parser);
        } else if (!Json_Strcmp(&parser, "modulation_depth")) {
            parser.pos++;
            properties.flModulationDepth = AL_ParseJsonFloat(&parser);
        } else if (!Json_Strcmp(&parser, "air_absorbtion_hf") || !Json_Strcmp(&parser, "air_absorption_hf")) {
            parser.pos++;
            properties.flAirAbsorptionGainHF = AL_ParseJsonFloat(&parser);
        } else if (!Json_Strcmp(&parser, "hf_reference")) {
            parser.pos++;
            properties.flHFReference = AL_ParseJsonFloat(&parser);
        } else if (!Json_Strcmp(&parser, "lf_reference")) {
            parser.pos++;
            properties.flLFReference = AL_ParseJsonFloat(&parser);
        } else if (!Json_Strcmp(&parser, "room_rolloff")) {
            parser.pos++;
            properties.flRoomRolloffFactor = AL_ParseJsonFloat(&parser);
        } else if (!Json_Strcmp(&parser, "decay_hf_limit")) {
            parser.pos++;
            properties.iDecayHFLimit = AL_ParseJsonInt(&parser);
        } else {
            parser.pos++;
            Json_SkipToken(&parser);
        }
    }

    Json_Free(&parser);
    return properties;
}

static void AL_LoadEAXEffectProfiles(void)
{
    for (int i = 0; i < SOUND_EAX_EFFECT_MAX; i++)
        s_eax_effects[i] = s_eax_default_properties;

    s_eax_effects[SOUND_EAX_EFFECT_DEFAULT] = s_eax_default_properties;
    s_eax_effects[SOUND_EAX_EFFECT_UNDERWATER] = s_eax_underwater_properties;

    for (int i = SOUND_EAX_EFFECT_ABANDONED; i < SOUND_EAX_EFFECT_MAX; i++) {
        if (!s_eax_json_names[i])
            continue;
        s_eax_effects[i] = AL_LoadEAXPropertiesFromJSON(s_eax_json_names[i]);
    }

    s_eax_current_id = SOUND_EAX_EFFECT_DEFAULT;
    s_eax_previous_id = SOUND_EAX_EFFECT_DEFAULT;
    s_eax_lerp_fraction = 1.0f;
    s_eax_mixed_properties = s_eax_effects[SOUND_EAX_EFFECT_DEFAULT];
}

static void AL_CompactAudioName(const char *in, char *out, size_t out_size)
{
    size_t o = 0;
    while (in && *in && o + 1 < out_size) {
        char c = *in++;
        if (c == '_' || c == '-' || c == ' ')
            continue;
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        out[o++] = c;
    }
    out[o] = '\0';
}

static bool AL_AudioNamesEqual(const char *a, const char *b)
{
    char compact_a[64], compact_b[64];
    AL_CompactAudioName(a, compact_a, sizeof(compact_a));
    AL_CompactAudioName(b, compact_b, sizeof(compact_b));
    return !Q_strcasecmp(compact_a, compact_b);
}

static int AL_EAXEffectIDForName(const char *name)
{
    if (!name || !name[0])
        return -1;

    if (AL_AudioNamesEqual(name, "default") || AL_AudioNamesEqual(name, "generic"))
        return SOUND_EAX_EFFECT_DEFAULT;
    if (AL_AudioNamesEqual(name, "underwater"))
        return SOUND_EAX_EFFECT_UNDERWATER;

    for (int i = SOUND_EAX_EFFECT_ABANDONED; i < SOUND_EAX_EFFECT_MAX; i++) {
        if (s_eax_json_names[i] && AL_AudioNamesEqual(name, s_eax_json_names[i]))
            return i;
    }

    return -1;
}

static int AL_ParseEAXEffectIDString(const char *value, int fallback)
{
    if (!value || !value[0])
        return fallback;

    const int named_id = AL_EAXEffectIDForName(value);
    if (named_id >= 0)
        return named_id;

    bool numeric = true;
    const char *p = value;
    if (*p == '-' || *p == '+')
        p++;
    if (!*p)
        numeric = false;
    while (*p) {
        if (*p < '0' || *p > '9') {
            numeric = false;
            break;
        }
        p++;
    }

    return numeric ? atoi(value) : fallback;
}

static void AL_ClearEAXZones(void)
{
    Z_Free(s_eax_zones);
    s_eax_zones = NULL;
    s_num_eax_zones = 0;
}

static bool AL_IsEAXZoneClass(const char *classname)
{
    return !Q_strcasecmp(classname, "client_env_sound") || !Q_strcasecmp(classname, "env_sound");
}

static bool AL_ParseOriginString(const char *value, vec3_t out)
{
    return sscanf(value, "%f %f %f", &out[0], &out[1], &out[2]) == 3;
}

static void AL_AddEAXZone(const vec3_t origin, float radius, qhandle_t reverb_id)
{
    size_t new_count = s_num_eax_zones + 1;
    s_eax_zones = static_cast<al_eax_zone_t *>(
        Z_Realloc(s_eax_zones, sizeof(*s_eax_zones) * new_count));
    al_eax_zone_t *zone = &s_eax_zones[s_num_eax_zones];
    VectorCopy(origin, zone->origin);
    zone->radius = radius;
    zone->reverb_id = Q_clip(reverb_id, 0, SOUND_EAX_EFFECT_MAX - 1);
    s_num_eax_zones = new_count;
}

static void AL_LoadEAXZones(void)
{
    AL_ClearEAXZones();

    if (!cl.bsp || !cl.bsp->entitystring)
        return;

    const char *data = cl.bsp->entitystring;
    char classname[MAX_QPATH];
    char origin_string[64];
    vec3_t zone_origin;
    bool in_entity = false;
    bool have_origin = false;
    float radius = 0.0f;
    int reverb_id = SOUND_EAX_EFFECT_DEFAULT;

    classname[0] = '\0';
    origin_string[0] = '\0';

    while (true) {
        char *token = COM_Parse(&data);
        if (!token[0])
            break;

        if (!strcmp(token, "{")) {
            in_entity = true;
            classname[0] = '\0';
            origin_string[0] = '\0';
            have_origin = false;
            radius = 0.0f;
            reverb_id = SOUND_EAX_EFFECT_DEFAULT;
            continue;
        }

        if (!strcmp(token, "}")) {
            if (in_entity && AL_IsEAXZoneClass(classname) && have_origin && radius > 0.0f)
                AL_AddEAXZone(zone_origin, radius, reverb_id);
            in_entity = false;
            continue;
        }

        if (!in_entity)
            continue;

        char key[MAX_QPATH];
        Q_strlcpy(key, token, sizeof(key));
        token = COM_Parse(&data);
        if (!token[0])
            break;

        if (!Q_strcasecmp(key, "classname")) {
            Q_strlcpy(classname, token, sizeof(classname));
        } else if (!Q_strcasecmp(key, "origin")) {
            Q_strlcpy(origin_string, token, sizeof(origin_string));
            have_origin = AL_ParseOriginString(origin_string, zone_origin);
        } else if (!Q_strcasecmp(key, "radius")) {
            radius = (float)atof(token);
        } else if (!Q_strcasecmp(key, "reverb_effect_id")) {
            reverb_id = atoi(token);
        } else if (!Q_strcasecmp(key, "reverb_effect") ||
                   !Q_strcasecmp(key, "reverb") ||
                   !Q_strcasecmp(key, "eax_profile")) {
            reverb_id = AL_ParseEAXEffectIDString(token, reverb_id);
        }
    }
}

static void AL_InterpolateEAX(const sfx_eax_properties_t *from, const sfx_eax_properties_t *to, float frac, sfx_eax_properties_t *out)
{
#define EAX_LERP(name) out->name = FASTLERP(from->name, to->name, frac)
    EAX_LERP(flDensity);
    EAX_LERP(flDiffusion);
    EAX_LERP(flGain);
    EAX_LERP(flGainHF);
    EAX_LERP(flGainLF);
    EAX_LERP(flDecayTime);
    EAX_LERP(flDecayHFRatio);
    EAX_LERP(flDecayLFRatio);
    EAX_LERP(flReflectionsGain);
    EAX_LERP(flReflectionsDelay);
    EAX_LERP(flReflectionsPan[0]);
    EAX_LERP(flReflectionsPan[1]);
    EAX_LERP(flReflectionsPan[2]);
    EAX_LERP(flLateReverbGain);
    EAX_LERP(flLateReverbDelay);
    EAX_LERP(flLateReverbPan[0]);
    EAX_LERP(flLateReverbPan[1]);
    EAX_LERP(flLateReverbPan[2]);
    EAX_LERP(flEchoTime);
    EAX_LERP(flEchoDepth);
    EAX_LERP(flModulationTime);
    EAX_LERP(flModulationDepth);
    EAX_LERP(flAirAbsorptionGainHF);
    EAX_LERP(flHFReference);
    EAX_LERP(flLFReference);
    EAX_LERP(flRoomRolloffFactor);
    out->iDecayHFLimit = frac < 0.5f ? from->iDecayHFLimit : to->iDecayHFLimit;
#undef EAX_LERP
}

static bool AL_IsEAXZoneReachable(const al_eax_zone_t *zone)
{
    trace_t tr;
    CL_Trace(&tr, zone->origin, listener_origin, vec3_origin, vec3_origin, NULL, MASK_SOLID);
    if (tr.fraction >= 1.0f)
        return true;

    // Fallback probe ring prevents false negatives when center-point LOS is blocked.
    const float probe_distance = Q_clipf(zone->radius * 0.2f, 32.0f, 192.0f);
    static const vec3_t probe_dirs[] = {
        { 1.0f, 0.0f, 0.0f },
        { -1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
        { 0.0f, -1.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f },
        { 0.0f, 0.0f, -1.0f }
    };

    for (size_t i = 0; i < q_countof(probe_dirs); i++) {
        vec3_t probe_origin;
        VectorMA(zone->origin, probe_distance, probe_dirs[i], probe_origin);
        CL_Trace(&tr, probe_origin, listener_origin, vec3_origin, vec3_origin, NULL, MASK_SOLID);
        if (tr.fraction >= 1.0f)
            return true;
    }

    return false;
}

static qhandle_t AL_DetermineEAXEnvironment(void)
{
    if (S_IsUnderWater())
        return SOUND_EAX_EFFECT_UNDERWATER;

    qhandle_t best = SOUND_EAX_EFFECT_DEFAULT;
    float best_dist_sq = FLT_MAX;

    for (size_t i = 0; i < s_num_eax_zones; i++) {
        const al_eax_zone_t *zone = &s_eax_zones[i];
        vec3_t delta;
        VectorSubtract(zone->origin, listener_origin, delta);
        float dist_sq = DotProduct(delta, delta);
        float radius_sq = zone->radius * zone->radius;
        if (dist_sq > radius_sq || dist_sq > best_dist_sq)
            continue;

        if (!AL_IsEAXZoneReachable(zone))
            continue;

        best_dist_sq = dist_sq;
        best = zone->reverb_id;
    }

    return best;
}

static bool AL_UpdateEAXEnvironment(void)
{
    if (!al_eax || !al_eax->integer || !s_reverb_effect || !s_reverb_slot)
        return false;

    qhandle_t target = AL_DetermineEAXEnvironment();
    target = Q_clip(target, 0, SOUND_EAX_EFFECT_MAX - 1);
    if (target == SOUND_EAX_EFFECT_DEFAULT)
        return false;

    if (target != s_eax_current_id) {
        s_eax_previous_id = s_eax_current_id;
        s_eax_current_id = target;
        s_eax_lerp_fraction = 0.0f;
    }

    float lerp_time = al_eax_lerp_time ? Cvar_ClampValue(al_eax_lerp_time, 0.0f, 10.0f) : 0.0f;
    if (lerp_time <= 0.0f) {
        s_eax_lerp_fraction = 1.0f;
    } else {
        float step = Q_clipf(cls.frametime, 0.0f, 0.25f) / lerp_time;
        s_eax_lerp_fraction = min(1.0f, s_eax_lerp_fraction + step);
    }

    AL_InterpolateEAX(&s_eax_effects[s_eax_previous_id], &s_eax_effects[s_eax_current_id],
                      s_eax_lerp_fraction, &s_eax_mixed_properties);
    S_SetEAXEnvironmentProperties(&s_eax_mixed_properties);
    s_reverb_driver = AL_REVERB_DRIVER_EAX;
    return true;
}

static const vec3_t             s_reverb_probes[] = {
    { 0.00000000f,    0.00000000f,     -1.00000000f },
    { 0.00000000f,    0.00000000f,     1.00000000f },
    { 0.707106769f,   0.00000000f,     0.707106769f },
    { 0.353553385f,   0.612372458f,    0.707106769f },
    { -0.353553444f,  0.612372458f,    0.707106769f },
    { -0.707106769f, -6.18172393e-08f, 0.707106769f },
    { -0.353553325f, -0.612372518f,    0.707106769f },
    { 0.353553355f,  -0.612372458f,    0.707106769f },
    { 1.00000000f,   0.00000000f,      -4.37113883e-08f },
    { 0.499999970f,  0.866025448f,     -4.37113883e-08f },
    { -0.500000060f, 0.866025388f,     -4.37113883e-08f },
    { -1.00000000f,  -8.74227766e-08f, -4.37113883e-08f },
    { -0.499999911f, -0.866025448f,    -4.37113883e-08f },
    { 0.499999911f,  -0.866025448f,    -4.37113883e-08f },
};
static int                      s_reverb_probe_time;
static int                      s_reverb_probe_index;
static vec3_t                   s_reverb_probe_results[q_countof(s_reverb_probes)];
static bool                     s_reverb_probe_sky[q_countof(s_reverb_probes)];
static bool                     s_reverb_probe_valid[q_countof(s_reverb_probes)];
static float                    s_reverb_probe_avg;

static const al_reverb_environment_t  *s_reverb_active_environment;

typedef enum {
    AL_ACOUSTIC_GROUP_DEFAULT,
    AL_ACOUSTIC_GROUP_SKY,
    AL_ACOUSTIC_GROUP_GLASS,
    AL_ACOUSTIC_GROUP_GRATE,
    AL_ACOUSTIC_GROUP_SOFT,
    AL_ACOUSTIC_GROUP_FOLIAGE,
    AL_ACOUSTIC_GROUP_WOOD,
    AL_ACOUSTIC_GROUP_METAL,
    AL_ACOUSTIC_GROUP_STONE,
    AL_ACOUSTIC_GROUP_WATER,
    AL_ACOUSTIC_GROUP_COUNT
} al_acoustic_group_t;

#define AL_ACOUSTIC_REGION_MATERIALS 16
#define AL_ACOUSTIC_REGION_NEIGHBORS 32

typedef struct {
    int16_t             step_id;
    al_acoustic_group_t group;
    float               weight;
} al_acoustic_material_weight_t;

typedef struct {
    bool                        valid;
    bool                        authored;
    bool                        authored_exterior_score;
    int                         area;
    int                         leaf_count;
    vec3_t                      mins, maxs;
    float                       dimension;
    float                       horizontal_openness;
    float                       vertical_openness;
    float                       sky_ratio;
    float                       enclosed_ratio;
    float                       portal_openness;
    float                       exterior_score;
    float                       group_weights[AL_ACOUSTIC_GROUP_COUNT];
    float                       group_weight_total;
    al_acoustic_material_weight_t materials[AL_ACOUSTIC_REGION_MATERIALS];
    size_t                      num_materials;
    al_acoustic_group_t         dominant_group;
    int16_t                     dominant_step_id;
    int                         portal_neighbors[AL_ACOUSTIC_REGION_NEIGHBORS];
    size_t                      num_portal_neighbors;
    int                         portal_count;
} al_acoustic_region_t;

typedef struct {
    int                         from_area;
    int                         to_area;
    float                       openness;
    float                       transmission;
    float                       gainhf;
} al_acoustic_portal_hint_t;

typedef struct {
    bool                        valid;
    const al_acoustic_region_t  *region;
    int                         area;
    int                         cluster;
    float                       dimension;
    float                       horizontal_openness;
    float                       vertical_openness;
    float                       sky_ratio;
    float                       enclosed_ratio;
    float                       portal_openness;
    float                       exterior_score;
    bool                        semi_open;
    bool                        exterior;
    float                       group_weights[AL_ACOUSTIC_GROUP_COUNT];
    float                       group_weight_total;
    al_acoustic_material_weight_t materials[AL_ACOUSTIC_REGION_MATERIALS];
    size_t                      num_materials;
    al_acoustic_group_t         dominant_group;
    int16_t                     dominant_step_id;
    bool                        floor_valid;
    int16_t                     floor_step_id;
    al_acoustic_group_t         floor_group;
} al_acoustic_space_t;

static al_acoustic_region_t     *s_acoustic_regions;
static size_t                   s_num_acoustic_regions;
static al_acoustic_space_t      s_listener_acoustic_space;
static int                      s_listener_acoustic_space_time;
static al_acoustic_portal_hint_t *s_acoustic_portal_hints;
static size_t                   s_num_acoustic_portal_hints;
static size_t                   s_num_acoustic_sidecar_region_hints;
static size_t                   s_num_acoustic_sidecar_eax_zones;
static char                     s_acoustic_sidecar_path[MAX_QPATH];

static al_acoustic_group_t AL_AcousticGroupForMaterial(const char *material, int flags)
{
    if (flags & SURF_SKY)
        return AL_ACOUSTIC_GROUP_SKY;

    if (!material || !material[0])
        return AL_ACOUSTIC_GROUP_DEFAULT;

    if (Q_stristr(material, "sky"))
        return AL_ACOUSTIC_GROUP_SKY;
    if (Q_stristr(material, "glass") || Q_stristr(material, "window") ||
        Q_stristr(material, "ice") || Q_stristr(material, "forcefield"))
        return AL_ACOUSTIC_GROUP_GLASS;
    if (Q_stristr(material, "grate") || Q_stristr(material, "grid") ||
        Q_stristr(material, "vent") || Q_stristr(material, "fence") ||
        Q_stristr(material, "chain") || Q_stristr(material, "grill") ||
        Q_stristr(material, "mesh") || Q_stristr(material, "screen") ||
        Q_stristr(material, "ladder") || Q_stristr(material, "bars") ||
        Q_stristr(material, "railing"))
        return AL_ACOUSTIC_GROUP_GRATE;
    if (Q_stristr(material, "grass") || Q_stristr(material, "foliage") ||
        Q_stristr(material, "leaves"))
        return AL_ACOUSTIC_GROUP_FOLIAGE;
    if (Q_stristr(material, "cloth") || Q_stristr(material, "curtain") ||
        Q_stristr(material, "fabric") || Q_stristr(material, "carpet") ||
        Q_stristr(material, "plaster") || Q_stristr(material, "drywall") ||
        Q_stristr(material, "sheetrock") || Q_stristr(material, "dirt") ||
        Q_stristr(material, "mud") || Q_stristr(material, "snow") ||
        Q_stristr(material, "sand") || Q_stristr(material, "gravel") ||
        Q_stristr(material, "flesh"))
        return AL_ACOUSTIC_GROUP_SOFT;
    if (Q_stristr(material, "wood") || Q_stristr(material, "plywood"))
        return AL_ACOUSTIC_GROUP_WOOD;
    if (Q_stristr(material, "metal") || Q_stristr(material, "steel") ||
        Q_stristr(material, "iron"))
        return AL_ACOUSTIC_GROUP_METAL;
    if (Q_stristr(material, "water") || Q_stristr(material, "slime") ||
        Q_stristr(material, "lava"))
        return AL_ACOUSTIC_GROUP_WATER;
    if (Q_stristr(material, "concrete") || Q_stristr(material, "cement") ||
        Q_stristr(material, "stone") || Q_stristr(material, "rock") ||
        Q_stristr(material, "brick") || Q_stristr(material, "asphalt") ||
        Q_stristr(material, "marble") || Q_stristr(material, "tile") ||
        Q_stristr(material, "ceramic"))
        return AL_ACOUSTIC_GROUP_STONE;

    return AL_ACOUSTIC_GROUP_DEFAULT;
}

static void AL_AddAcousticMaterial(al_acoustic_material_weight_t *materials, size_t *num_materials,
                                   int16_t step_id, al_acoustic_group_t group, float weight)
{
    if (weight <= 0.0f)
        return;

    for (size_t i = 0; i < *num_materials; i++) {
        if (materials[i].step_id == step_id) {
            materials[i].weight += weight;
            if (materials[i].group == AL_ACOUSTIC_GROUP_DEFAULT)
                materials[i].group = group;
            return;
        }
    }

    if (*num_materials < AL_ACOUSTIC_REGION_MATERIALS) {
        materials[*num_materials].step_id = step_id;
        materials[*num_materials].group = group;
        materials[*num_materials].weight = weight;
        (*num_materials)++;
        return;
    }

    size_t weakest = 0;
    for (size_t i = 1; i < *num_materials; i++) {
        if (materials[i].weight < materials[weakest].weight)
            weakest = i;
    }

    if (weight > materials[weakest].weight) {
        materials[weakest].step_id = step_id;
        materials[weakest].group = group;
        materials[weakest].weight = weight;
    }
}

static al_acoustic_group_t AL_DominantAcousticGroup(const float *weights, float total)
{
    if (total <= 0.0f)
        return AL_ACOUSTIC_GROUP_DEFAULT;

    al_acoustic_group_t best = AL_ACOUSTIC_GROUP_DEFAULT;
    for (int i = 1; i < AL_ACOUSTIC_GROUP_COUNT; i++) {
        if (weights[i] > weights[best])
            best = static_cast<al_acoustic_group_t>(i);
    }
    return best;
}

static int16_t AL_DominantAcousticStep(const al_acoustic_material_weight_t *materials, size_t num_materials)
{
    if (!num_materials)
        return FOOTSTEP_ID_DEFAULT;

    size_t best = 0;
    for (size_t i = 1; i < num_materials; i++) {
        if (materials[i].weight > materials[best].weight)
            best = i;
    }
    return materials[best].step_id;
}

static float AL_ComputeExteriorScore(float sky_ratio, float vertical_openness,
                                     float portal_openness, float enclosed_ratio,
                                     al_acoustic_group_t dominant_group)
{
    float material_hint = 0.0f;
    if (dominant_group == AL_ACOUSTIC_GROUP_SKY ||
        dominant_group == AL_ACOUSTIC_GROUP_FOLIAGE)
        material_hint = 1.0f;
    else if (dominant_group == AL_ACOUSTIC_GROUP_WATER)
        material_hint = 0.35f;

    return Q_clipf(sky_ratio * 0.35f +
                   vertical_openness * 0.25f +
                   portal_openness * 0.15f +
                   (1.0f - enclosed_ratio) * 0.20f +
                   material_hint * 0.05f,
                   0.0f, 1.0f);
}

static void AL_ClearAcousticSidecar(void)
{
    Z_Free(s_acoustic_portal_hints);
    s_acoustic_portal_hints = NULL;
    s_num_acoustic_portal_hints = 0;
    s_num_acoustic_sidecar_region_hints = 0;
    s_num_acoustic_sidecar_eax_zones = 0;
    s_acoustic_sidecar_path[0] = '\0';
}

static void AL_FreeAcousticRegions(void)
{
    AL_ClearAcousticSidecar();

    if (s_acoustic_regions) {
        Z_Free(s_acoustic_regions);
        s_acoustic_regions = NULL;
    }
    s_num_acoustic_regions = 0;
    memset(&s_listener_acoustic_space, 0, sizeof(s_listener_acoustic_space));
    s_listener_acoustic_space_time = 0;
}

static void AL_AddAcousticRegionNeighbor(al_acoustic_region_t *region, int other_area)
{
    if (other_area < 0)
        return;

    for (size_t i = 0; i < region->num_portal_neighbors; i++) {
        if (region->portal_neighbors[i] == other_area)
            return;
    }

    if (region->num_portal_neighbors < AL_ACOUSTIC_REGION_NEIGHBORS)
        region->portal_neighbors[region->num_portal_neighbors++] = other_area;
}

static void AL_AddAcousticRegionFace(al_acoustic_region_t *region, const mface_t *face)
{
    if (!face || !face->texinfo)
        return;

    const mtexinfo_t *texinfo = face->texinfo;
    const char *material = texinfo->c.material[0] ? texinfo->c.material : texinfo->name;
    float weight = max(1.0f, static_cast<float>(face->numsurfedges));
    al_acoustic_group_t group = AL_AcousticGroupForMaterial(material, texinfo->c.flags);

    region->group_weights[group] += weight;
    region->group_weight_total += weight;
    AL_AddAcousticMaterial(region->materials, &region->num_materials,
                           static_cast<int16_t>(texinfo->step_id), group, weight);
}

static void AL_FinalizeAcousticRegion(al_acoustic_region_t *region)
{
    if (!region->valid) {
        region->dominant_group = AL_ACOUSTIC_GROUP_DEFAULT;
        region->dominant_step_id = FOOTSTEP_ID_DEFAULT;
        region->enclosed_ratio = 1.0f;
        return;
    }

    vec3_t extents;
    VectorSubtract(region->maxs, region->mins, extents);

    float x = max(extents[0], 0.0f);
    float y = max(extents[1], 0.0f);
    float z = max(extents[2], 0.0f);
    region->dimension = max(128.0f, (x + y + z) / 3.0f);
    region->horizontal_openness = Q_clipf(((x + y) * 0.5f) / 2048.0f, 0.0f, 1.0f);
    region->vertical_openness = Q_clipf(z / 1536.0f, 0.0f, 1.0f);
    region->portal_openness = Q_clipf(static_cast<float>(region->num_portal_neighbors) / 4.0f, 0.0f, 1.0f);

    if (region->group_weight_total > 0.0f) {
        region->sky_ratio = Q_clipf(region->group_weights[AL_ACOUSTIC_GROUP_SKY] /
                                        region->group_weight_total,
                                    0.0f, 1.0f);
        region->enclosed_ratio = Q_clipf(1.0f - region->sky_ratio, 0.0f, 1.0f);
    } else {
        region->sky_ratio = 0.0f;
        region->enclosed_ratio = 1.0f;
    }

    region->dominant_group =
        AL_DominantAcousticGroup(region->group_weights, region->group_weight_total);
    region->dominant_step_id =
        AL_DominantAcousticStep(region->materials, region->num_materials);
    region->exterior_score = AL_ComputeExteriorScore(region->sky_ratio,
                                                     region->vertical_openness,
                                                     region->portal_openness,
                                                     region->enclosed_ratio,
                                                     region->dominant_group);
}

static void AL_BuildAcousticRegions(void)
{
    AL_FreeAcousticRegions();

    if (!cl.bsp || cl.bsp->numareas <= 0 || !cl.bsp->areas)
        return;

    s_num_acoustic_regions = cl.bsp->numareas;
    s_acoustic_regions = static_cast<al_acoustic_region_t *>(
        Z_TagMallocz(sizeof(*s_acoustic_regions) * s_num_acoustic_regions, TAG_SOUND));

    for (size_t i = 0; i < s_num_acoustic_regions; i++) {
        al_acoustic_region_t *region = &s_acoustic_regions[i];
        region->area = static_cast<int>(i);
        ClearBounds(region->mins, region->maxs);
    }

    for (int i = 0; i < cl.bsp->numleafs; i++) {
        const mleaf_t *leaf = &cl.bsp->leafs[i];
        if ((leaf->contents[0] & CONTENTS_SOLID) || leaf->area < 0 ||
            static_cast<size_t>(leaf->area) >= s_num_acoustic_regions)
            continue;

        al_acoustic_region_t *region = &s_acoustic_regions[leaf->area];
        region->valid = true;
        region->leaf_count++;
        AddPointToBounds(leaf->mins, region->mins, region->maxs);
        AddPointToBounds(leaf->maxs, region->mins, region->maxs);

        for (int f = 0; f < leaf->numleaffaces; f++)
            AL_AddAcousticRegionFace(region, leaf->firstleafface[f]);
    }

    for (size_t i = 0; i < s_num_acoustic_regions; i++) {
        al_acoustic_region_t *region = &s_acoustic_regions[i];
        if (static_cast<int>(i) >= cl.bsp->numareas)
            continue;

        const marea_t *area = &cl.bsp->areas[i];
        region->portal_count = area->numareaportals;
        for (int p = 0; p < area->numareaportals; p++) {
            const mareaportal_t *portal = &area->firstareaportal[p];
            AL_AddAcousticRegionNeighbor(region, static_cast<int>(portal->otherarea));
        }
    }

    for (size_t i = 0; i < s_num_acoustic_regions; i++)
        AL_FinalizeAcousticRegion(&s_acoustic_regions[i]);
}

static const al_acoustic_region_t *AL_AcousticRegionForPoint(const vec3_t point, const mleaf_t **out_leaf)
{
    if (out_leaf)
        *out_leaf = NULL;

    if (!cl.bsp || !cl.bsp->nodes || !s_acoustic_regions)
        return NULL;

    const mleaf_t *leaf = BSP_PointLeaf(cl.bsp->nodes, point);
    if (out_leaf)
        *out_leaf = leaf;

    if (!leaf || leaf->area < 0 || static_cast<size_t>(leaf->area) >= s_num_acoustic_regions)
        return NULL;

    const al_acoustic_region_t *region = &s_acoustic_regions[leaf->area];
    return region->valid ? region : NULL;
}

static float AL_ReverbProbeSkyRatio(void)
{
    float sky = 0.0f;
    float total = 0.0f;

    for (size_t i = 0; i < q_countof(s_reverb_probes); i++) {
        if (!s_reverb_probe_valid[i])
            continue;
        total += 1.0f;
        if (s_reverb_probe_sky[i])
            sky += 1.0f;
    }

    return total > 0.0f ? sky / total : 0.0f;
}

static float AL_ReverbProbeHorizontalOpenness(void)
{
    float total = 0.0f;
    float count = 0.0f;

    for (size_t i = 8; i < q_countof(s_reverb_probes); i++) {
        if (!s_reverb_probe_valid[i])
            continue;
        total += VectorLength(s_reverb_probe_results[i]);
        count += 1.0f;
    }

    return count > 0.0f ? Q_clipf((total / count) / 2048.0f, 0.0f, 1.0f) : 0.0f;
}

static float AL_ReverbProbeVerticalOpenness(void)
{
    if (!s_reverb_probe_valid[0] && !s_reverb_probe_valid[1])
        return 0.0f;

    float up = s_reverb_probe_valid[1] ? fabsf(s_reverb_probe_results[1][2]) : 0.0f;
    float down = s_reverb_probe_valid[0] ? fabsf(s_reverb_probe_results[0][2]) : 0.0f;
    return Q_clipf((up + down) / 2048.0f, 0.0f, 1.0f);
}

static void AL_AddMaterialToSpace(al_acoustic_space_t *space, int16_t step_id,
                                  al_acoustic_group_t group, float weight)
{
    if (weight <= 0.0f)
        return;

    space->group_weights[group] += weight;
    space->group_weight_total += weight;
    AL_AddAcousticMaterial(space->materials, &space->num_materials, step_id, group, weight);
    space->dominant_group = AL_DominantAcousticGroup(space->group_weights,
                                                     space->group_weight_total);
    space->dominant_step_id = AL_DominantAcousticStep(space->materials,
                                                      space->num_materials);
}

static bool AL_TraceFloorAcoustics(const vec3_t origin, int16_t *step_id,
                                   al_acoustic_group_t *group)
{
    trace_t tr;
    const vec3_t mins = { -16, -16, 0 };
    const vec3_t maxs = { 16, 16, 0 };
    const vec3_t start = { origin[0], origin[1], origin[2] + 1.0f };
    const vec3_t down = { start[0], start[1], start[2] - 256.0f };

    CL_Trace(&tr, start, mins, maxs, down, NULL, MASK_SOLID);
    if (tr.fraction >= 1.0f || !tr.surface || !tr.surface->id)
        return false;

    uint32_t texinfo_index = tr.surface->id - 1;
    if (texinfo_index >= static_cast<uint32_t>(cl.bsp->numtexinfo))
        return false;

    const mtexinfo_t *texinfo = &cl.bsp->texinfo[texinfo_index];
    const char *material = texinfo->c.material[0] ? texinfo->c.material : texinfo->name;

    if (step_id)
        *step_id = static_cast<int16_t>(texinfo->step_id);
    if (group)
        *group = AL_AcousticGroupForMaterial(material, texinfo->c.flags);
    return true;
}

static void AL_ClassifyAcousticSpace(const vec3_t origin, bool use_listener_probes,
                                     al_acoustic_space_t *space)
{
    memset(space, 0, sizeof(*space));
    space->area = -1;
    space->cluster = -1;
    space->dominant_group = AL_ACOUSTIC_GROUP_DEFAULT;
    space->dominant_step_id = FOOTSTEP_ID_DEFAULT;
    space->floor_step_id = FOOTSTEP_ID_DEFAULT;
    space->floor_group = AL_ACOUSTIC_GROUP_DEFAULT;
    space->dimension = 384.0f;
    space->enclosed_ratio = 1.0f;

    const mleaf_t *leaf = NULL;
    const al_acoustic_region_t *region = AL_AcousticRegionForPoint(origin, &leaf);
    if (leaf) {
        space->cluster = leaf->cluster;
        space->area = leaf->area;
    }

    if (region) {
        space->valid = true;
        space->region = region;
        space->dimension = region->dimension;
        space->horizontal_openness = region->horizontal_openness;
        space->vertical_openness = region->vertical_openness;
        space->sky_ratio = region->sky_ratio;
        space->enclosed_ratio = region->enclosed_ratio;
        space->portal_openness = region->portal_openness;
        memcpy(space->group_weights, region->group_weights, sizeof(space->group_weights));
        space->group_weight_total = region->group_weight_total;
        memcpy(space->materials, region->materials, sizeof(space->materials));
        space->num_materials = region->num_materials;
        space->dominant_group = region->dominant_group;
        space->dominant_step_id = region->dominant_step_id;
    }

    if (use_listener_probes) {
        float probe_sky = AL_ReverbProbeSkyRatio();
        float probe_horizontal = AL_ReverbProbeHorizontalOpenness();
        float probe_vertical = AL_ReverbProbeVerticalOpenness();

        if (s_reverb_probe_avg > 32.0f)
            space->dimension = FASTLERP(space->dimension, s_reverb_probe_avg, 0.65f);

        space->sky_ratio = Q_clipf(max(space->sky_ratio, probe_sky), 0.0f, 1.0f);
        space->horizontal_openness = Q_clipf(max(space->horizontal_openness, probe_horizontal), 0.0f, 1.0f);
        space->vertical_openness = Q_clipf(max(space->vertical_openness, probe_vertical), 0.0f, 1.0f);
        space->enclosed_ratio = Q_clipf(min(space->enclosed_ratio, 1.0f - probe_sky), 0.0f, 1.0f);
    }

    int16_t floor_step_id = FOOTSTEP_ID_DEFAULT;
    al_acoustic_group_t floor_group = AL_ACOUSTIC_GROUP_DEFAULT;
    if (use_listener_probes && AL_TraceFloorAcoustics(origin, &floor_step_id, &floor_group)) {
        space->floor_valid = true;
        space->floor_step_id = floor_step_id;
        space->floor_group = floor_group;
        AL_AddMaterialToSpace(space, floor_step_id, floor_group, 3.0f);
    }

    space->dominant_group = AL_DominantAcousticGroup(space->group_weights,
                                                     space->group_weight_total);
    space->dominant_step_id = AL_DominantAcousticStep(space->materials,
                                                      space->num_materials);
    if (region && region->authored_exterior_score) {
        space->exterior_score = region->exterior_score;
    } else {
        space->exterior_score = AL_ComputeExteriorScore(space->sky_ratio,
                                                        space->vertical_openness,
                                                        space->portal_openness,
                                                        space->enclosed_ratio,
                                                        space->dominant_group);
    }
    space->semi_open = space->exterior_score >= 0.35f;
    space->exterior = space->exterior_score >= 0.58f;

    if (space->exterior && s_num_reverb_environments)
        space->dimension = max(space->dimension,
                               s_reverb_environments[s_num_reverb_environments - 1].dimension);
}

static bool AL_SelectReverbEnvironment(float dimension)
{
    if (!s_reverb_environments || !s_num_reverb_environments)
        return false;

    const al_reverb_environment_t *target = s_reverb_environments;
    while (target != s_reverb_environments + s_num_reverb_environments - 1 &&
           dimension > target->dimension) {
        target++;
    }

    if (target == s_reverb_active_environment)
        return false;

    s_reverb_active_environment = target;
    return true;
}

static bool AL_EstimateDimensions(void)
{
    if (!s_reverb_environments)
        return false;

    if (s_reverb_probe_time > cl.time)
        return false;

    s_reverb_probe_time = cl.time + 13;
    vec3_t end;
    VectorMA(listener_origin, 8192.0f, s_reverb_probes[s_reverb_probe_index], end);

    trace_t tr;
    CL_Trace(&tr, listener_origin, end, vec3_origin, vec3_origin, NULL, MASK_SOLID);

    VectorSubtract(tr.endpos, listener_origin, s_reverb_probe_results[s_reverb_probe_index]);
    s_reverb_probe_valid[s_reverb_probe_index] = true;
    s_reverb_probe_sky[s_reverb_probe_index] = tr.surface && (tr.surface->flags & SURF_SKY);

    if (s_reverb_probe_index == 1 && s_reverb_probe_sky[s_reverb_probe_index]) {
        s_reverb_probe_results[s_reverb_probe_index][2] += 4096.f;
    }

    vec3_t mins, maxs;
    ClearBounds(mins, maxs);

    for (size_t i = 0; i < q_countof(s_reverb_probes); i++)
        AddPointToBounds(s_reverb_probe_results[i], mins, maxs);

    vec3_t extents;
    VectorSubtract(maxs, mins, extents);

    s_reverb_probe_avg = (extents[0] + extents[1] + extents[2]) / 3.0f;

    s_reverb_probe_index = (s_reverb_probe_index + 1) % q_countof(s_reverb_probes);
    return true;
}

static inline float AL_CalculateReverbFrac(void)
{
    float frac = (cl.time - (float) s_reverb_lerp_start) / (s_reverb_lerp_time - (float) s_reverb_lerp_start);
    float bfrac = 1.0f - frac;
    float f = Q_clipf(1.0f - (bfrac * bfrac * bfrac), 0.0f, 1.0f);
    return f;
}

static bool AL_IsOutdoorPreset(uint8_t preset)
{
    return preset >= 14 && preset <= 20;
}

static float AL_SpaceMaterialStepScore(const al_acoustic_space_t *space, int16_t step_id)
{
    if (!space || space->group_weight_total <= 0.0f)
        return 0.0f;

    float weight = 0.0f;
    for (size_t i = 0; i < space->num_materials; i++) {
        if (space->materials[i].step_id == step_id)
            weight += space->materials[i].weight;
    }

    return Q_clipf(weight / space->group_weight_total, 0.0f, 1.0f);
}

static float AL_SpaceMaterialGroupScore(const al_acoustic_space_t *space, al_acoustic_group_t group)
{
    if (!space || group < 0 || group >= AL_ACOUSTIC_GROUP_COUNT ||
        space->group_weight_total <= 0.0f)
        return 0.0f;

    return Q_clipf(space->group_weights[group] / space->group_weight_total, 0.0f, 1.0f);
}

static uint8_t AL_DefaultPresetForAcousticSpace(const al_acoustic_space_t *space)
{
    if (!space)
        return 0;

    if (space->dominant_group == AL_ACOUSTIC_GROUP_WATER)
        return 22;

    if (space->exterior) {
        if (space->dominant_group == AL_ACOUSTIC_GROUP_STONE)
            return 18;
        if (space->dominant_group == AL_ACOUSTIC_GROUP_METAL)
            return 20;
        if (space->dominant_group == AL_ACOUSTIC_GROUP_FOLIAGE)
            return 15;
        if (space->semi_open)
            return 19;
        return 16;
    }

    if (space->dimension > 1536.0f) {
        if (space->dominant_group == AL_ACOUSTIC_GROUP_STONE)
            return 8;
        if (space->dominant_group == AL_ACOUSTIC_GROUP_METAL)
            return 10;
        return 9;
    }

    if (space->dimension > 768.0f) {
        if (space->dominant_group == AL_ACOUSTIC_GROUP_STONE)
            return 13;
        if (space->dominant_group == AL_ACOUSTIC_GROUP_METAL)
            return 12;
        if (space->dominant_group == AL_ACOUSTIC_GROUP_SOFT)
            return 11;
        return 12;
    }

    if (space->dominant_group == AL_ACOUSTIC_GROUP_STONE)
        return 5;
    if (space->dominant_group == AL_ACOUSTIC_GROUP_SOFT)
        return 4;
    if (space->dominant_group == AL_ACOUSTIC_GROUP_METAL)
        return 2;

    return 2;
}

static uint8_t AL_SelectReverbPresetForAcousticSpace(const al_acoustic_space_t *space)
{
    if (!space || !s_reverb_active_environment)
        return s_reverb_current_preset;

    uint8_t fallback_preset = AL_DefaultPresetForAcousticSpace(space);
    float best_score = -1.0f;
    uint8_t best_preset = fallback_preset;
    bool found_entry_fallback = false;
    uint8_t entry_fallback_preset = fallback_preset;

    for (size_t i = 0; i < s_reverb_active_environment->num_reverbs; i++) {
        const al_reverb_entry_t *entry = &s_reverb_active_environment->reverbs[i];

        if (!entry->num_materials) {
            found_entry_fallback = true;
            entry_fallback_preset = entry->preset;
            continue;
        }

        float score = 0.0f;
        for (size_t m = 0; m < entry->num_materials; m++) {
            score += AL_SpaceMaterialStepScore(space, entry->materials[m].step_id);

            al_acoustic_group_t entry_group =
                AL_AcousticGroupForMaterial(entry->materials[m].material, 0);
            score += AL_SpaceMaterialGroupScore(space, entry_group) * 0.35f;
        }

        if (space->exterior && AL_IsOutdoorPreset(entry->preset))
            score += 0.20f;
        else if (!space->semi_open && AL_IsOutdoorPreset(entry->preset))
            score *= 0.5f;

        if (score > best_score) {
            best_score = score;
            best_preset = entry->preset;
        }
    }

    if (best_score > 0.05f)
        return best_preset;

    if (found_entry_fallback && !space->exterior)
        return entry_fallback_preset;

    return fallback_preset;
}

static void AL_UpdateReverb(bool force)
{
    if (!s_reverb_environments || !s_reverb_active_environment)
        return;

    if (!cl.bsp)
        return;

    AL_EstimateDimensions();
    AL_ClassifyAcousticSpace(listener_origin, true, &s_listener_acoustic_space);
    s_listener_acoustic_space_time = cl.time;
    bool environment_changed = AL_SelectReverbEnvironment(s_listener_acoustic_space.dimension);
    uint8_t new_preset = AL_SelectReverbPresetForAcousticSpace(&s_listener_acoustic_space);
    force = force || environment_changed;

    if (force || new_preset != s_reverb_current_preset) {
        if (force) {
            s_reverb_lerp_time = 0;
        } else if (s_reverb_lerp_time) {
            memcpy(&s_active_reverb, &s_reverb_lerp_result, sizeof(s_reverb_lerp_result));
        }

        s_reverb_current_preset = new_preset;
        memcpy(&s_reverb_lerp_to, &s_reverb_parameters[s_reverb_current_preset], sizeof(s_active_reverb));

        float lerp_seconds = al_reverb_lerp_time ? Cvar_ClampValue(al_reverb_lerp_time, 0.0f, 10.0f) : 0.0f;
        if (force || lerp_seconds <= 0.0f) {
            s_reverb_lerp_time = 0;
            memcpy(&s_active_reverb, &s_reverb_lerp_to, sizeof(s_active_reverb));
            memcpy(&s_reverb_lerp_result, &s_active_reverb, sizeof(s_reverb_lerp_result));
            AL_LoadEffect(&s_active_reverb);
            s_reverb_driver = AL_REVERB_DRIVER_AUTO;
            return;
        }

        s_reverb_lerp_start = cl.time;
        s_reverb_lerp_time = cl.time + static_cast<int>(lerp_seconds * 1000.0f);
    }

    if (s_reverb_lerp_time) {
        if (cl.time >= s_reverb_lerp_time) {
            s_reverb_lerp_time = 0;
            memcpy(&s_active_reverb, &s_reverb_lerp_to, sizeof(s_active_reverb));
            AL_LoadEffect(&s_active_reverb);
        } else {
            float f = AL_CalculateReverbFrac();

#define AL_LERP(prop) \
                s_reverb_lerp_result.prop = FASTLERP(s_active_reverb.prop, s_reverb_lerp_to.prop, f)
            
            AL_LERP(flDensity);
            AL_LERP(flDiffusion);
            AL_LERP(flGain);
            AL_LERP(flGainHF);
            AL_LERP(flGainLF);
            AL_LERP(flDecayTime);
            AL_LERP(flDecayHFRatio);
            AL_LERP(flDecayLFRatio);
            AL_LERP(flReflectionsGain);
            AL_LERP(flReflectionsDelay);
            AL_LERP(flReflectionsPan[0]);
            AL_LERP(flReflectionsPan[1]);
            AL_LERP(flReflectionsPan[2]);
            AL_LERP(flLateReverbGain);
            AL_LERP(flLateReverbDelay);
            AL_LERP(flLateReverbPan[0]);
            AL_LERP(flLateReverbPan[1]);
            AL_LERP(flLateReverbPan[2]);
            AL_LERP(flEchoTime);
            AL_LERP(flEchoDepth);
            AL_LERP(flModulationTime);
            AL_LERP(flModulationDepth);
            AL_LERP(flAirAbsorptionGainHF);
            AL_LERP(flHFReference);
            AL_LERP(flLFReference);
            AL_LERP(flRoomRolloffFactor);
            AL_LERP(iDecayHFLimit);

            AL_LoadEffect(&s_reverb_lerp_result);
        }
    }

    s_reverb_driver = AL_REVERB_DRIVER_AUTO;
}

static void AL_LoadReverbEntry(json_parse_t *parser, al_reverb_entry_t *out_entry)
{
    size_t fields = parser->pos->size;
    Json_EnsureNext(parser, JSMN_OBJECT);

    for (size_t i = 0; i < fields; i++) {
        if (!Json_Strcmp(parser, "materials")) {
            parser->pos++;

            if (parser->pos->type == JSMN_STRING) {
                if (parser->buffer[parser->pos->start] != '*')
                    Json_Error(parser, parser->pos, "expected string to start with *\n");

                parser->pos++;
            } else {
                size_t n = parser->pos->size;
                Json_EnsureNext(parser, JSMN_ARRAY);
                out_entry->num_materials = n;
                out_entry->materials = static_cast<al_reverb_material_t *>(Z_TagMalloc(sizeof(*out_entry->materials) * n, TAG_SOUND));

                for (size_t m = 0; m < n; m++, parser->pos++) {
                    Json_Ensure(parser, JSMN_STRING);
                    Q_strnlcpy(out_entry->materials[m].material,
                        parser->buffer + parser->pos->start,
                        parser->pos->end - parser->pos->start,
                        sizeof(out_entry->materials[m].material));
                }
            }

        } else if (!Json_Strcmp(parser, "preset")) {
            parser->pos++;

            Json_Ensure(parser, JSMN_STRING);
            size_t p = 0;

            for (; p < q_countof(s_reverb_names); p++)
                if (!Json_Strcmp(parser, s_reverb_names[p]))
                    break;

            if (p == q_countof(s_reverb_names)) {
                Com_WPrintf("missing sound environment preset\n");
                out_entry->preset = 19; // plain
            } else {
                out_entry->preset = p;
            }

            parser->pos++;
        } else {
            parser->pos++;
            Json_SkipToken(parser);
        }
    }
}

static void AL_LoadReverbEnvironment(json_parse_t *parser, al_reverb_environment_t *out_environment)
{
    size_t fields = parser->pos->size;
    Json_EnsureNext(parser, JSMN_OBJECT);

    for (size_t i = 0; i < fields; i++) {
        if (!Json_Strcmp(parser, "dimension")) {
            Json_Next(parser);
            Json_Ensure(parser, JSMN_PRIMITIVE);
            out_environment->dimension = atof(parser->buffer + parser->pos->start);
            parser->pos++;
        } else if (!Json_Strcmp(parser, "reverbs")) {
            Json_Next(parser);

            out_environment->num_reverbs = parser->pos->size;
            Json_EnsureNext(parser, JSMN_ARRAY);
            out_environment->reverbs = static_cast<al_reverb_entry_t *>(Z_TagMallocz(sizeof(al_reverb_entry_t) * out_environment->num_reverbs, TAG_SOUND));

            for (size_t r = 0; r < out_environment->num_reverbs; r++)
                AL_LoadReverbEntry(parser, &out_environment->reverbs[r]);
        } else {
            parser->pos++;
            Json_SkipToken(parser);
        }
    }
}

static void AL_FreeReverbEnvironments(al_reverb_environment_t *environments, size_t num_environments)
{
    for (size_t i = 0; i < num_environments; i++) {
        for (size_t n = 0; n < environments[i].num_reverbs; n++) {
            Z_Free(environments[i].reverbs[n].materials);
        }

        Z_Free(environments[i].reverbs);
    }

    Z_Free(environments);
}

typedef struct {
    const char *const *materials;
    size_t            num_materials;
    uint8_t           preset;
} al_builtin_reverb_entry_t;

typedef struct {
    float                           dimension;
    const al_builtin_reverb_entry_t *entries;
    size_t                          num_entries;
} al_builtin_reverb_environment_t;

static const char *const s_builtin_stone_materials[] = {
    "stone", "concrete", "cement", "rock", "brick"
};

static const char *const s_builtin_metal_materials[] = {
    "metal", "steel", "iron"
};

static const char *const s_builtin_soft_materials[] = {
    "cloth", "fabric", "carpet", "dirt", "mud", "snow"
};

static const char *const s_builtin_exterior_materials[] = {
    "grass", "foliage", "sand", "gravel", "dirt", "snow"
};

static const al_builtin_reverb_entry_t s_builtin_reverb_small[] = {
    { s_builtin_soft_materials, q_countof(s_builtin_soft_materials), 11 },
    { s_builtin_stone_materials, q_countof(s_builtin_stone_materials), 5 },
    { s_builtin_metal_materials, q_countof(s_builtin_metal_materials), 2 },
    { NULL, 0, 2 }
};

static const al_builtin_reverb_entry_t s_builtin_reverb_medium[] = {
    { s_builtin_soft_materials, q_countof(s_builtin_soft_materials), 11 },
    { s_builtin_stone_materials, q_countof(s_builtin_stone_materials), 13 },
    { s_builtin_metal_materials, q_countof(s_builtin_metal_materials), 12 },
    { NULL, 0, 12 }
};

static const al_builtin_reverb_entry_t s_builtin_reverb_large[] = {
    { s_builtin_stone_materials, q_countof(s_builtin_stone_materials), 8 },
    { s_builtin_metal_materials, q_countof(s_builtin_metal_materials), 10 },
    { NULL, 0, 9 }
};

static const al_builtin_reverb_entry_t s_builtin_reverb_exterior[] = {
    { s_builtin_exterior_materials, q_countof(s_builtin_exterior_materials), 19 },
    { s_builtin_stone_materials, q_countof(s_builtin_stone_materials), 18 },
    { s_builtin_metal_materials, q_countof(s_builtin_metal_materials), 20 },
    { NULL, 0, 19 }
};

static const al_builtin_reverb_environment_t s_builtin_reverb_environments[] = {
    { 384.0f, s_builtin_reverb_small, q_countof(s_builtin_reverb_small) },
    { 768.0f, s_builtin_reverb_medium, q_countof(s_builtin_reverb_medium) },
    { 1536.0f, s_builtin_reverb_large, q_countof(s_builtin_reverb_large) },
    { 4096.0f, s_builtin_reverb_exterior, q_countof(s_builtin_reverb_exterior) }
};

static void AL_ResetReverbState(void)
{
    s_reverb_active_environment = s_num_reverb_environments ? s_reverb_environments : NULL;
    s_reverb_probe_time = 0;
    s_reverb_probe_index = 0;
    s_reverb_probe_avg = 0.0f;
    memset(s_reverb_probe_results, 0, sizeof(s_reverb_probe_results));
    memset(s_reverb_probe_sky, 0, sizeof(s_reverb_probe_sky));
    memset(s_reverb_probe_valid, 0, sizeof(s_reverb_probe_valid));
    memset(&s_listener_acoustic_space, 0, sizeof(s_listener_acoustic_space));
    s_listener_acoustic_space_time = 0;
    s_reverb_lerp_start = 0;
    s_reverb_lerp_time = 0;
    s_reverb_current_preset = 0;
    s_reverb_driver = AL_REVERB_DRIVER_NONE;

    memcpy(&s_active_reverb, &s_reverb_parameters[s_reverb_current_preset], sizeof(s_active_reverb));
    memcpy(&s_reverb_lerp_to, &s_active_reverb, sizeof(s_reverb_lerp_to));
    memcpy(&s_reverb_lerp_result, &s_active_reverb, sizeof(s_reverb_lerp_result));

    if (s_reverb_effect && s_reverb_slot)
        AL_LoadEffect(&s_active_reverb);
}

static void AL_UseBuiltInReverbEnvironments(void)
{
    const size_t n = q_countof(s_builtin_reverb_environments);
    al_reverb_environment_t *environments = static_cast<al_reverb_environment_t *>(
        Z_TagMallocz(sizeof(*environments) * n, TAG_SOUND));

    for (size_t i = 0; i < n; i++) {
        const al_builtin_reverb_environment_t *src_env = &s_builtin_reverb_environments[i];
        al_reverb_environment_t *dst_env = &environments[i];

        dst_env->dimension = src_env->dimension;
        dst_env->num_reverbs = src_env->num_entries;
        dst_env->reverbs = static_cast<al_reverb_entry_t *>(
            Z_TagMallocz(sizeof(*dst_env->reverbs) * dst_env->num_reverbs, TAG_SOUND));

        for (size_t r = 0; r < dst_env->num_reverbs; r++) {
            const al_builtin_reverb_entry_t *src_entry = &src_env->entries[r];
            al_reverb_entry_t *dst_entry = &dst_env->reverbs[r];

            dst_entry->preset = src_entry->preset;
            dst_entry->num_materials = src_entry->num_materials;

            if (!src_entry->num_materials)
                continue;

            dst_entry->materials = static_cast<al_reverb_material_t *>(
                Z_TagMallocz(sizeof(*dst_entry->materials) * dst_entry->num_materials, TAG_SOUND));

            for (size_t m = 0; m < dst_entry->num_materials; m++) {
                Q_strlcpy(dst_entry->materials[m].material, src_entry->materials[m],
                          sizeof(dst_entry->materials[m].material));
            }
        }
    }

    s_reverb_environments = environments;
    s_num_reverb_environments = n;
    AL_ResetReverbState();
}

static int16_t AL_FindStepID(const char *material)
{
    if (!strcmp(material, "") || !strcmp(material, "default"))
        return FOOTSTEP_ID_DEFAULT;
    else if (!strcmp(material, "ladder"))
        return FOOTSTEP_ID_LADDER;

    mtexinfo_t *out;
    int i;

    // FIXME: can speed this up later with a hash map of some sort
    for (i = 0, out = cl.bsp->texinfo; i < cl.bsp->numtexinfo; i++, out++) {
        if (!strcmp(out->c.material, material)) {
            return out->step_id;
        }
    }

    return FOOTSTEP_ID_DEFAULT;
}

static void AL_SetReverbStepIDs(void)
{
    for (size_t i = 0; i < s_num_reverb_environments; i++) {
        for (size_t n = 0; n < s_reverb_environments[i].num_reverbs; n++) {
            al_reverb_entry_t *entry = &s_reverb_environments[i].reverbs[n];

            for (size_t e = 0; e < entry->num_materials; e++) {
                entry->materials[e].step_id = AL_FindStepID(entry->materials[e].material);
            }
        }
    }
}

static const al_acoustic_region_t *AL_AcousticRegionByArea(int area);
static al_acoustic_region_t *AL_MutableAcousticRegionByArea(int area);
static void AL_AddAcousticPortalHint(int from_area, int to_area, float openness,
                                     float transmission, float gainhf,
                                     bool bidirectional);

typedef struct {
    bool        has_area;
    int         area;
    bool        has_origin;
    vec3_t      origin;
    bool        has_dimension;
    float       dimension;
    bool        has_horizontal_openness;
    float       horizontal_openness;
    bool        has_vertical_openness;
    float       vertical_openness;
    bool        has_sky_ratio;
    float       sky_ratio;
    bool        has_enclosed_ratio;
    float       enclosed_ratio;
    bool        has_portal_openness;
    float       portal_openness;
    bool        has_exterior_score;
    float       exterior_score;
    bool        has_material;
    bool        force_material;
    char        material[64];
    bool        has_step_id;
    int         step_id;
} al_acoustic_region_hint_t;

typedef struct {
    bool        has_from_area;
    int         from_area;
    bool        has_to_area;
    int         to_area;
    bool        has_from_origin;
    vec3_t      from_origin;
    bool        has_to_origin;
    vec3_t      to_origin;
    float       openness;
    float       transmission;
    float       gainhf;
    bool        bidirectional;
} al_acoustic_portal_hint_parse_t;

typedef struct {
    bool        has_origin;
    vec3_t      origin;
    float       radius;
    int         reverb_id;
} al_acoustic_eax_zone_hint_t;

static bool AL_ParseJsonVec3Any(json_parse_t *parser, vec3_t out)
{
    if (parser->pos->type == JSMN_ARRAY) {
        AL_ParseJsonVec3(parser, out);
        return true;
    }

    if (parser->pos->type == JSMN_STRING) {
        char buffer[64];
        AL_ParseJsonString(parser, buffer, sizeof(buffer));
        return AL_ParseOriginString(buffer, out);
    }

    Json_Error(parser, parser->pos, "expected vec3 array or origin string\n");
    return false;
}

static int AL_ParseJsonEAXEffectID(json_parse_t *parser, int fallback)
{
    if (parser->pos->type == JSMN_STRING) {
        char buffer[64];
        AL_ParseJsonString(parser, buffer, sizeof(buffer));
        return AL_ParseEAXEffectIDString(buffer, fallback);
    }

    return AL_ParseJsonInt(parser);
}

static void AL_AppendAcousticRegionHint(al_acoustic_region_hint_t **hints,
                                        size_t *num_hints,
                                        const al_acoustic_region_hint_t *hint)
{
    size_t new_count = *num_hints + 1;
    *hints = static_cast<al_acoustic_region_hint_t *>(
        Z_Realloc(*hints, sizeof(**hints) * new_count));
    (*hints)[*num_hints] = *hint;
    *num_hints = new_count;
}

static void AL_AppendAcousticPortalHint(al_acoustic_portal_hint_parse_t **hints,
                                        size_t *num_hints,
                                        const al_acoustic_portal_hint_parse_t *hint)
{
    size_t new_count = *num_hints + 1;
    *hints = static_cast<al_acoustic_portal_hint_parse_t *>(
        Z_Realloc(*hints, sizeof(**hints) * new_count));
    (*hints)[*num_hints] = *hint;
    *num_hints = new_count;
}

static void AL_AppendAcousticEAXZoneHint(al_acoustic_eax_zone_hint_t **hints,
                                         size_t *num_hints,
                                         const al_acoustic_eax_zone_hint_t *hint)
{
    size_t new_count = *num_hints + 1;
    *hints = static_cast<al_acoustic_eax_zone_hint_t *>(
        Z_Realloc(*hints, sizeof(**hints) * new_count));
    (*hints)[*num_hints] = *hint;
    *num_hints = new_count;
}

static void AL_ParseAcousticRegionHint(json_parse_t *parser,
                                       al_acoustic_region_hint_t **hints,
                                       size_t *num_hints)
{
    al_acoustic_region_hint_t hint{};
    hint.area = -1;
    hint.step_id = FOOTSTEP_ID_DEFAULT;

    size_t fields = parser->pos->size;
    Json_EnsureNext(parser, JSMN_OBJECT);

    for (size_t i = 0; i < fields; i++) {
        if (!Json_Strcmp(parser, "area") || !Json_Strcmp(parser, "area_id")) {
            parser->pos++;
            hint.area = AL_ParseJsonInt(parser);
            hint.has_area = true;
        } else if (!Json_Strcmp(parser, "origin")) {
            parser->pos++;
            hint.has_origin = AL_ParseJsonVec3Any(parser, hint.origin);
        } else if (!Json_Strcmp(parser, "dimension")) {
            parser->pos++;
            hint.dimension = AL_ParseJsonFloat(parser);
            hint.has_dimension = true;
        } else if (!Json_Strcmp(parser, "horizontal_openness")) {
            parser->pos++;
            hint.horizontal_openness = AL_ParseJsonFloat(parser);
            hint.has_horizontal_openness = true;
        } else if (!Json_Strcmp(parser, "vertical_openness")) {
            parser->pos++;
            hint.vertical_openness = AL_ParseJsonFloat(parser);
            hint.has_vertical_openness = true;
        } else if (!Json_Strcmp(parser, "sky_ratio")) {
            parser->pos++;
            hint.sky_ratio = AL_ParseJsonFloat(parser);
            hint.has_sky_ratio = true;
        } else if (!Json_Strcmp(parser, "enclosed_ratio")) {
            parser->pos++;
            hint.enclosed_ratio = AL_ParseJsonFloat(parser);
            hint.has_enclosed_ratio = true;
        } else if (!Json_Strcmp(parser, "portal_openness")) {
            parser->pos++;
            hint.portal_openness = AL_ParseJsonFloat(parser);
            hint.has_portal_openness = true;
        } else if (!Json_Strcmp(parser, "exterior_score")) {
            parser->pos++;
            hint.exterior_score = AL_ParseJsonFloat(parser);
            hint.has_exterior_score = true;
        } else if (!Json_Strcmp(parser, "material")) {
            parser->pos++;
            AL_ParseJsonString(parser, hint.material, sizeof(hint.material));
            hint.has_material = true;
        } else if (!Json_Strcmp(parser, "dominant_material")) {
            parser->pos++;
            AL_ParseJsonString(parser, hint.material, sizeof(hint.material));
            hint.has_material = true;
            hint.force_material = true;
        } else if (!Json_Strcmp(parser, "step_id")) {
            parser->pos++;
            hint.step_id = AL_ParseJsonInt(parser);
            hint.has_step_id = true;
        } else {
            parser->pos++;
            Json_SkipToken(parser);
        }
    }

    AL_AppendAcousticRegionHint(hints, num_hints, &hint);
}

static void AL_ParseAcousticPortalHint(json_parse_t *parser,
                                       al_acoustic_portal_hint_parse_t **hints,
                                       size_t *num_hints)
{
    al_acoustic_portal_hint_parse_t hint{};
    hint.from_area = -1;
    hint.to_area = -1;
    hint.openness = 0.75f;
    hint.transmission = 1.0f;
    hint.gainhf = 1.0f;
    hint.bidirectional = true;

    size_t fields = parser->pos->size;
    Json_EnsureNext(parser, JSMN_OBJECT);

    for (size_t i = 0; i < fields; i++) {
        if (!Json_Strcmp(parser, "from") || !Json_Strcmp(parser, "from_area") ||
            !Json_Strcmp(parser, "area_from")) {
            parser->pos++;
            hint.from_area = AL_ParseJsonInt(parser);
            hint.has_from_area = true;
        } else if (!Json_Strcmp(parser, "to") || !Json_Strcmp(parser, "to_area") ||
                   !Json_Strcmp(parser, "area_to")) {
            parser->pos++;
            hint.to_area = AL_ParseJsonInt(parser);
            hint.has_to_area = true;
        } else if (!Json_Strcmp(parser, "from_origin")) {
            parser->pos++;
            hint.has_from_origin = AL_ParseJsonVec3Any(parser, hint.from_origin);
        } else if (!Json_Strcmp(parser, "to_origin")) {
            parser->pos++;
            hint.has_to_origin = AL_ParseJsonVec3Any(parser, hint.to_origin);
        } else if (!Json_Strcmp(parser, "openness")) {
            parser->pos++;
            hint.openness = AL_ParseJsonFloat(parser);
        } else if (!Json_Strcmp(parser, "transmission") || !Json_Strcmp(parser, "gain")) {
            parser->pos++;
            hint.transmission = AL_ParseJsonFloat(parser);
        } else if (!Json_Strcmp(parser, "gain_hf") || !Json_Strcmp(parser, "gainhf")) {
            parser->pos++;
            hint.gainhf = AL_ParseJsonFloat(parser);
        } else if (!Json_Strcmp(parser, "bidirectional")) {
            parser->pos++;
            hint.bidirectional = AL_ParseJsonBool(parser, hint.bidirectional);
        } else {
            parser->pos++;
            Json_SkipToken(parser);
        }
    }

    AL_AppendAcousticPortalHint(hints, num_hints, &hint);
}

static void AL_ParseAcousticEAXZoneHint(json_parse_t *parser,
                                        al_acoustic_eax_zone_hint_t **hints,
                                        size_t *num_hints)
{
    al_acoustic_eax_zone_hint_t hint{};
    hint.radius = 0.0f;
    hint.reverb_id = SOUND_EAX_EFFECT_DEFAULT;

    size_t fields = parser->pos->size;
    Json_EnsureNext(parser, JSMN_OBJECT);

    for (size_t i = 0; i < fields; i++) {
        if (!Json_Strcmp(parser, "origin")) {
            parser->pos++;
            hint.has_origin = AL_ParseJsonVec3Any(parser, hint.origin);
        } else if (!Json_Strcmp(parser, "radius")) {
            parser->pos++;
            hint.radius = AL_ParseJsonFloat(parser);
        } else if (!Json_Strcmp(parser, "reverb_effect_id")) {
            parser->pos++;
            hint.reverb_id = AL_ParseJsonEAXEffectID(parser, hint.reverb_id);
        } else if (!Json_Strcmp(parser, "reverb_effect") ||
                   !Json_Strcmp(parser, "reverb") ||
                   !Json_Strcmp(parser, "eax_profile")) {
            parser->pos++;
            hint.reverb_id = AL_ParseJsonEAXEffectID(parser, hint.reverb_id);
        } else {
            parser->pos++;
            Json_SkipToken(parser);
        }
    }

    AL_AppendAcousticEAXZoneHint(hints, num_hints, &hint);
}

static void AL_ParseAcousticRegionHints(json_parse_t *parser,
                                        al_acoustic_region_hint_t **hints,
                                        size_t *num_hints)
{
    size_t count = parser->pos->size;
    Json_EnsureNext(parser, JSMN_ARRAY);

    for (size_t i = 0; i < count; i++)
        AL_ParseAcousticRegionHint(parser, hints, num_hints);
}

static void AL_ParseAcousticPortalHints(json_parse_t *parser,
                                        al_acoustic_portal_hint_parse_t **hints,
                                        size_t *num_hints)
{
    size_t count = parser->pos->size;
    Json_EnsureNext(parser, JSMN_ARRAY);

    for (size_t i = 0; i < count; i++)
        AL_ParseAcousticPortalHint(parser, hints, num_hints);
}

static void AL_ParseAcousticEAXZoneHints(json_parse_t *parser,
                                         al_acoustic_eax_zone_hint_t **hints,
                                         size_t *num_hints)
{
    size_t count = parser->pos->size;
    Json_EnsureNext(parser, JSMN_ARRAY);

    for (size_t i = 0; i < count; i++)
        AL_ParseAcousticEAXZoneHint(parser, hints, num_hints);
}

static void AL_ParseAcousticSidecar(json_parse_t *parser,
                                    al_acoustic_region_hint_t **region_hints,
                                    size_t *num_region_hints,
                                    al_acoustic_portal_hint_parse_t **portal_hints,
                                    size_t *num_portal_hints,
                                    al_acoustic_eax_zone_hint_t **zone_hints,
                                    size_t *num_zone_hints)
{
    size_t fields = Json_EnsureNext(parser, JSMN_OBJECT)->size;

    for (size_t i = 0; i < fields; i++) {
        if (!Json_Strcmp(parser, "regions")) {
            parser->pos++;
            AL_ParseAcousticRegionHints(parser, region_hints, num_region_hints);
        } else if (!Json_Strcmp(parser, "portals") || !Json_Strcmp(parser, "openings")) {
            parser->pos++;
            AL_ParseAcousticPortalHints(parser, portal_hints, num_portal_hints);
        } else if (!Json_Strcmp(parser, "eax_zones") ||
                   !Json_Strcmp(parser, "env_sounds") ||
                   !Json_Strcmp(parser, "environment_zones")) {
            parser->pos++;
            AL_ParseAcousticEAXZoneHints(parser, zone_hints, num_zone_hints);
        } else {
            parser->pos++;
            Json_SkipToken(parser);
        }
    }
}

static int AL_AcousticAreaForOrigin(const vec3_t origin)
{
    const mleaf_t *leaf = NULL;
    AL_AcousticRegionForPoint(origin, &leaf);

    if (!leaf)
        return -1;

    return leaf->area;
}

static bool AL_ApplyAcousticRegionHint(const al_acoustic_region_hint_t *hint)
{
    int area = hint->has_area ? hint->area :
               (hint->has_origin ? AL_AcousticAreaForOrigin(hint->origin) : -1);
    al_acoustic_region_t *region = AL_MutableAcousticRegionByArea(area);
    if (!region)
        return false;

    region->authored = true;

    if (hint->has_dimension)
        region->dimension = Q_clipf(hint->dimension, 64.0f, 8192.0f);
    if (hint->has_horizontal_openness)
        region->horizontal_openness = Q_clipf(hint->horizontal_openness, 0.0f, 1.0f);
    if (hint->has_vertical_openness)
        region->vertical_openness = Q_clipf(hint->vertical_openness, 0.0f, 1.0f);
    if (hint->has_sky_ratio)
        region->sky_ratio = Q_clipf(hint->sky_ratio, 0.0f, 1.0f);
    if (hint->has_enclosed_ratio)
        region->enclosed_ratio = Q_clipf(hint->enclosed_ratio, 0.0f, 1.0f);
    else if (hint->has_sky_ratio)
        region->enclosed_ratio = Q_clipf(1.0f - region->sky_ratio, 0.0f, 1.0f);
    if (hint->has_portal_openness)
        region->portal_openness = Q_clipf(hint->portal_openness, 0.0f, 1.0f);

    if (hint->has_material || hint->has_step_id) {
        al_acoustic_group_t group = hint->has_material ?
            AL_AcousticGroupForMaterial(hint->material, 0) : AL_ACOUSTIC_GROUP_DEFAULT;
        int16_t step_id = hint->has_step_id ? static_cast<int16_t>(hint->step_id) :
            (hint->has_material ? AL_FindStepID(hint->material) : FOOTSTEP_ID_DEFAULT);
        float weight = max(region->group_weight_total * 0.25f, 1.0f);

        if (hint->force_material) {
            weight = max(region->group_weight_total + 1.0f - region->group_weights[group],
                         1.0f);
        }

        region->group_weights[group] += weight;
        region->group_weight_total += weight;
        AL_AddAcousticMaterial(region->materials, &region->num_materials,
                               step_id, group, weight);
    }

    region->dominant_group =
        AL_DominantAcousticGroup(region->group_weights, region->group_weight_total);
    region->dominant_step_id =
        AL_DominantAcousticStep(region->materials, region->num_materials);
    if (hint->has_exterior_score) {
        region->authored_exterior_score = true;
        region->exterior_score = Q_clipf(hint->exterior_score, 0.0f, 1.0f);
    } else if (!region->authored_exterior_score) {
        region->exterior_score = AL_ComputeExteriorScore(region->sky_ratio,
                                                         region->vertical_openness,
                                                         region->portal_openness,
                                                         region->enclosed_ratio,
                                                         region->dominant_group);
    }

    return true;
}

static bool AL_ApplyAcousticPortalHint(const al_acoustic_portal_hint_parse_t *hint)
{
    int from_area = hint->has_from_area ? hint->from_area :
                    (hint->has_from_origin ? AL_AcousticAreaForOrigin(hint->from_origin) : -1);
    int to_area = hint->has_to_area ? hint->to_area :
                  (hint->has_to_origin ? AL_AcousticAreaForOrigin(hint->to_origin) : -1);

    if (!AL_AcousticRegionByArea(from_area) || !AL_AcousticRegionByArea(to_area) ||
        from_area == to_area)
        return false;

    AL_AddAcousticPortalHint(from_area, to_area, hint->openness, hint->transmission,
                             hint->gainhf, hint->bidirectional);
    return true;
}

static bool AL_ApplyAcousticEAXZoneHint(const al_acoustic_eax_zone_hint_t *hint)
{
    if (!hint->has_origin || hint->radius <= 0.0f)
        return false;

    AL_AddEAXZone(hint->origin, hint->radius, hint->reverb_id);
    return true;
}

typedef enum {
    AL_ACOUSTIC_SIDECAR_MISSING,
    AL_ACOUSTIC_SIDECAR_LOADED,
    AL_ACOUSTIC_SIDECAR_ERROR
} al_acoustic_sidecar_status_t;

static al_acoustic_sidecar_status_t AL_LoadAcousticSidecarPath(const char *path)
{
    json_parse_t parser{};
    al_acoustic_region_hint_t *region_hints = NULL;
    al_acoustic_portal_hint_parse_t *portal_hints = NULL;
    al_acoustic_eax_zone_hint_t *zone_hints = NULL;
    size_t num_region_hints = 0;
    size_t num_portal_hints = 0;
    size_t num_zone_hints = 0;

    if (Json_ErrorHandler(parser)) {
        bool missing = strstr(parser.error, "Couldn't load file") != NULL;
        if (!missing) {
            Com_WPrintf("Couldn't load acoustic sidecar %s[%s]; %s\n",
                        path, parser.error_loc, parser.error);
        }
        Json_Free(&parser);
        Z_Free(region_hints);
        Z_Free(portal_hints);
        Z_Free(zone_hints);
        return missing ? AL_ACOUSTIC_SIDECAR_MISSING : AL_ACOUSTIC_SIDECAR_ERROR;
    }

    Json_Load(path, &parser);
    AL_ParseAcousticSidecar(&parser,
                            &region_hints, &num_region_hints,
                            &portal_hints, &num_portal_hints,
                            &zone_hints, &num_zone_hints);
    Json_Free(&parser);

    for (size_t i = 0; i < num_region_hints; i++) {
        if (AL_ApplyAcousticRegionHint(&region_hints[i]))
            s_num_acoustic_sidecar_region_hints++;
    }

    for (size_t i = 0; i < num_portal_hints; i++)
        AL_ApplyAcousticPortalHint(&portal_hints[i]);

    for (size_t i = 0; i < num_zone_hints; i++) {
        if (AL_ApplyAcousticEAXZoneHint(&zone_hints[i]))
            s_num_acoustic_sidecar_eax_zones++;
    }

    Q_strlcpy(s_acoustic_sidecar_path, path, sizeof(s_acoustic_sidecar_path));
    Com_DPrintf("Loaded acoustic sidecar %s (%zu region hint%s, %zu portal edge%s, %zu EAX zone%s).\n",
                path,
                s_num_acoustic_sidecar_region_hints,
                s_num_acoustic_sidecar_region_hints == 1 ? "" : "s",
                s_num_acoustic_portal_hints,
                s_num_acoustic_portal_hints == 1 ? "" : "s",
                s_num_acoustic_sidecar_eax_zones,
                s_num_acoustic_sidecar_eax_zones == 1 ? "" : "s");

    Z_Free(region_hints);
    Z_Free(portal_hints);
    Z_Free(zone_hints);
    return AL_ACOUSTIC_SIDECAR_LOADED;
}

static void AL_LoadAcousticSidecar(void)
{
    if (!cl.mapname[0] || !s_acoustic_regions)
        return;

    char path[MAX_QPATH];
    if (Q_snprintf(path, sizeof(path), "maps/%s.aud", cl.mapname) < sizeof(path)) {
        al_acoustic_sidecar_status_t status = AL_LoadAcousticSidecarPath(path);
        if (status != AL_ACOUSTIC_SIDECAR_MISSING)
            return;
    }

    if (Q_snprintf(path, sizeof(path), "sound/acoustics/%s.aud", cl.mapname) < sizeof(path))
        AL_LoadAcousticSidecarPath(path);
}

static void AL_LoadReverbEnvironments(void)
{
    json_parse_t parser{};
    al_reverb_environment_t *environments = NULL;
    size_t n = 0;

    AL_FreeReverbEnvironments(s_reverb_environments, s_num_reverb_environments);
    s_reverb_environments = NULL;
    s_num_reverb_environments = 0;
    s_reverb_active_environment = NULL;

    if (Json_ErrorHandler(parser)) {
        if (strstr(parser.error, "Couldn't load file")) {
            Com_DPrintf("Using built-in sound/default.environments fallback.\n");
        } else {
            Com_WPrintf("Couldn't load sound/default.environments[%s]; %s; using built-in fallback\n",
                        parser.error_loc, parser.error);
        }
        Json_Free(&parser);
        AL_FreeReverbEnvironments(environments, n);
        AL_UseBuiltInReverbEnvironments();
        return;
    }

    Json_Load("sound/default.environments", &parser);

    Json_EnsureNext(&parser, JSMN_OBJECT);

    if (Json_Strcmp(&parser, "environments")) 
        Json_Error(&parser, parser.pos, "expected \"environments\" key\n");

    Json_Next(&parser);

    n = parser.pos->size;
    if (n == 0) {
        Json_Free(&parser);
        AL_UseBuiltInReverbEnvironments();
        return;
    }
    Json_EnsureNext(&parser, JSMN_ARRAY);

    environments = static_cast<al_reverb_environment_t *>(Z_TagMallocz(sizeof(al_reverb_environment_t) * n, TAG_SOUND));

    for (size_t i = 0; i < n; i++)
        AL_LoadReverbEnvironment(&parser, &environments[i]);

    s_reverb_environments = environments;
    s_num_reverb_environments = n;
    AL_ResetReverbState();

    Json_Free(&parser);
}

static void AL_Reverb_stat(void)
{
    SCR_StatKeyValue("dimensions", va("%g", s_reverb_probe_avg));
    SCR_StatKeyValue("env dim", s_reverb_active_environment ?
                     va("%g", s_reverb_active_environment->dimension) : "none");
    SCR_StatKeyValue("region dim", va("%g", s_listener_acoustic_space.dimension));
    SCR_StatKeyValue("region area", va("%d", s_listener_acoustic_space.area));
    SCR_StatKeyValue("region sky", va("%g", s_listener_acoustic_space.sky_ratio));
    SCR_StatKeyValue("region exterior", va("%g", s_listener_acoustic_space.exterior_score));
    SCR_StatKeyValue("aud sidecar", s_acoustic_sidecar_path[0] ? s_acoustic_sidecar_path : "none");
    SCR_StatKeyValue("aud hints", va("%zu/%zu/%zu",
                                      s_num_acoustic_sidecar_region_hints,
                                      s_num_acoustic_portal_hints,
                                      s_num_acoustic_sidecar_eax_zones));
    SCR_StatKeyValue("preset", s_reverb_names[s_reverb_current_preset]);

#define AL_STATF(e) SCR_StatKeyValue(#e, va("%g", s_reverb_lerp_result.e))
#define AL_STATI(e) SCR_StatKeyValue(#e, va("%d", s_reverb_lerp_result.e))
    
    AL_STATF(flDensity);
    AL_STATF(flDiffusion);
    AL_STATF(flGain);
    AL_STATF(flGainHF);
    AL_STATF(flGainLF);
    AL_STATF(flDecayTime);
    AL_STATF(flDecayHFRatio);
    AL_STATF(flDecayLFRatio);
    AL_STATF(flReflectionsGain);
    AL_STATF(flReflectionsDelay);
    AL_STATF(flLateReverbGain);
    AL_STATF(flLateReverbDelay);
    AL_STATF(flEchoTime);
    AL_STATF(flEchoDepth);
    AL_STATF(flModulationTime);
    AL_STATF(flModulationDepth);
    AL_STATF(flAirAbsorptionGainHF);
    AL_STATF(flHFReference);
    AL_STATF(flLFReference);
    AL_STATF(flRoomRolloffFactor);
    AL_STATI(iDecayHFLimit);

    SCR_StatKeyValue("lerp", !s_reverb_lerp_time ? "none" : va("%g", AL_CalculateReverbFrac()));
}

static void AL_StreamStop(void);
static void AL_StopChannel(channel_t *ch);

static void AL_SoundInfo(void)
{
    Com_Printf("AL_VENDOR: %s\n", qalGetString(AL_VENDOR));
    Com_Printf("AL_RENDERER: %s\n", qalGetString(AL_RENDERER));
    Com_Printf("AL_VERSION: %s\n", qalGetString(AL_VERSION));
    Com_Printf("AL_EXTENSIONS: %s\n", qalGetString(AL_EXTENSIONS));
    Com_Printf("Number of sources: %d\n", s_numchannels);
}

static void s_underwater_gain_hf_changed(cvar_t *self)
{
    if (s_underwater_flag) {
        for (int i = 0; i < s_numchannels; i++)
            qalSourcei(s_srcnums[i], AL_DIRECT_FILTER, 0);
        s_underwater_flag = false;
    }

    qalFilterf(s_underwater_filter, AL_LOWPASS_GAINHF, Cvar_ClampValue(self, 0.001f, 1));
}

static void al_reverb_changed(cvar_t *self)
{
    S_StopAllSounds();
}

static void al_merge_looping_changed(cvar_t *self)
{
    int         i;
    channel_t   *ch;

    for (i = 0, ch = s_channels; i < s_numchannels; i++, ch++) {
        if (ch->autosound)
            AL_StopChannel(ch);
    }
}

static void s_volume_changed(cvar_t *self)
{
    qalListenerf(AL_GAIN, self->value);
}

static void al_doppler_changed(cvar_t *self)
{
    float factor = Cvar_ClampValue(self, 0.0f, 4.0f);
    qalDopplerFactor(factor);
    float speed = AL_DOPPLER_SPEED;
    if (al_doppler_speed)
        speed = Cvar_ClampValue(al_doppler_speed, 4000.0f, 30000.0f);
    qalSpeedOfSound(speed);
}

static float AL_GetRolloffFactor(float dist_mult)
{
    if (al_distance_model && al_distance_model->integer)
        return dist_mult * SOUND_FULLVOLUME;

    return dist_mult * (8192 - SOUND_FULLVOLUME);
}

static void al_distance_model_changed(cvar_t *self)
{
    ALenum model = self->integer ? AL_INVERSE_DISTANCE_CLAMPED : AL_LINEAR_DISTANCE_CLAMPED;
    qalDistanceModel(model);

    if (!s_channels || !s_srcnums)
        return;

    for (int i = 0; i < s_numchannels; i++) {
        channel_t *ch = &s_channels[i];
        if (!ch->sfx)
            continue;
        if (al_merge_looping && al_merge_looping->integer >= s_merge_looping_minval &&
            ch->autosound && !ch->no_merge)
            continue;

        qalSourcef(ch->srcnum, AL_ROLLOFF_FACTOR, AL_GetRolloffFactor(ch->dist_mult));
    }
}

static float AL_DistanceFromListener(const vec3_t origin)
{
    vec3_t delta;
    VectorSubtract(origin, listener_origin, delta);
    return VectorLength(delta);
}

static float AL_ComputeAirAbsorption(float distance)
{
    if (!al_air_absorption || !al_air_absorption->integer)
        return 0.0f;

    float max_dist = Cvar_ClampValue(al_air_absorption_distance, 128.0f, 16384.0f);
    if (max_dist <= 0.0f)
        return 0.0f;

    return Q_clipf(distance / max_dist, 0.0f, 1.0f);
}

static float AL_GetOcclusionGainHF(const channel_t *ch, float occlusion_mix)
{
    if (occlusion_mix <= 0.0f)
        return 1.0f;

    float material_gainhf = S_OcclusionCutoffToGainHF(ch->occlusion_cutoff);
    return FASTLERP(1.0f, material_gainhf, occlusion_mix);
}

static float AL_GetOcclusionGain(float occlusion_mix)
{
    if (occlusion_mix <= 0.0f)
        return 1.0f;

    return FASTLERP(1.0f, S_OCCLUSION_GAIN, occlusion_mix);
}

static float AL_ComputeReverbSend(float distance, float occlusion_mix)
{
    if (!al_reverb_send || !al_reverb_send->integer)
        return 1.0f;

    float max_dist = Cvar_ClampValue(al_reverb_send_distance, 128.0f, 16384.0f);
    float min_send = Cvar_ClampValue(al_reverb_send_min, 0.0f, 1.0f);
    float send = max_dist > 0.0f ? Q_clipf(distance / max_dist, 0.0f, 1.0f) : 0.0f;

    send = max(send, min_send);

    if (occlusion_mix > 0.5f) {
        float boost = Cvar_ClampValue(al_reverb_send_occlusion_boost, 1.0f, 4.0f);
        send = min(send * boost, 1.0f);
    }

    return send;
}

static void AL_UpdateDirectFilter(channel_t *ch, float occlusion_mix, float air_absorption,
                                  bool apply_occlusion_gain)
{
    if (s_air_absorption_supported)
        qalSourcef(ch->srcnum, s_air_absorption_enum, air_absorption);

    bool use_air_filter = !s_air_absorption_supported && air_absorption > 0.001f;
    float gain = apply_occlusion_gain ? AL_GetOcclusionGain(occlusion_mix) : 1.0f;
    float gainhf = AL_GetOcclusionGainHF(ch, occlusion_mix);
    if (use_air_filter) {
        float air_gainhf = Q_clipf(1.0f - air_absorption, 0.0f, 1.0f);
        gainhf = Q_clipf(gainhf * air_gainhf, 0.0f, 1.0f);
    }

    ALint filter = 0;
    if (s_underwater_flag) {
        filter = s_underwater_filter;
    } else if (s_occlusion_filters && (occlusion_mix > 0.001f || use_air_filter)) {
        int ch_index = static_cast<int>(ch - s_channels);
        if (ch_index >= 0 && ch_index < s_num_occlusion_filters) {
            qalFilterf(s_occlusion_filters[ch_index], AL_LOWPASS_GAIN, gain);
            qalFilterf(s_occlusion_filters[ch_index], AL_LOWPASS_GAINHF, gainhf);
            filter = s_occlusion_filters[ch_index];
        }
    }

    qalSourcei(ch->srcnum, AL_DIRECT_FILTER, filter);
}

static al_acoustic_space_t *AL_GetListenerAcousticSpaceForSend(void)
{
    if (!cl.bsp)
        return NULL;

    if (s_listener_acoustic_space_time != cl.time) {
        AL_ClassifyAcousticSpace(listener_origin, false, &s_listener_acoustic_space);
        s_listener_acoustic_space_time = cl.time;
    }

    return s_listener_acoustic_space.valid ? &s_listener_acoustic_space : NULL;
}

static float AL_AcousticGroupSendGainHF(al_acoustic_group_t group)
{
    switch (group) {
    case AL_ACOUSTIC_GROUP_WATER:
        return 0.70f;
    case AL_ACOUSTIC_GROUP_STONE:
        return 0.82f;
    case AL_ACOUSTIC_GROUP_METAL:
        return 0.86f;
    case AL_ACOUSTIC_GROUP_SOFT:
        return 0.78f;
    case AL_ACOUSTIC_GROUP_FOLIAGE:
        return 0.88f;
    case AL_ACOUSTIC_GROUP_GLASS:
        return 0.92f;
    default:
        return 1.0f;
    }
}

typedef struct {
    bool    valid;
    int     hops;
    float   transmission;
    float   occlusion;
    float   cutoff_hz;
    float   reverb_gainhf;
    float   send_scale;
    float   send_gainhf;
} al_portal_path_t;

typedef enum {
    AL_SOURCE_PATH_LOCAL,
    AL_SOURCE_PATH_SAME_SPACE,
    AL_SOURCE_PATH_ADJACENT_SPACE,
    AL_SOURCE_PATH_CROSS_SPACE,
    AL_SOURCE_PATH_PORTAL,
    AL_SOURCE_PATH_EXTERIOR_TO_INTERIOR,
    AL_SOURCE_PATH_INTERIOR_TO_EXTERIOR,
    AL_SOURCE_PATH_UNREACHABLE
} al_source_path_kind_t;

typedef struct {
    al_source_path_kind_t       kind;
    const al_acoustic_space_t   *listener_space;
    al_acoustic_space_t         source_space;
    al_portal_path_t            portal_path;
    float                       occlusion_floor;
    float                       cutoff_ceiling;
    float                       reverb_gainhf_ceiling;
    float                       send_scale;
    float                       send_gainhf;
} al_source_path_state_t;

static const al_acoustic_region_t *AL_AcousticRegionByArea(int area)
{
    if (area < 0 || static_cast<size_t>(area) >= s_num_acoustic_regions)
        return NULL;

    const al_acoustic_region_t *region = &s_acoustic_regions[area];
    return region->valid ? region : NULL;
}

static al_acoustic_region_t *AL_MutableAcousticRegionByArea(int area)
{
    if (area < 0 || static_cast<size_t>(area) >= s_num_acoustic_regions)
        return NULL;

    al_acoustic_region_t *region = &s_acoustic_regions[area];
    return region->valid ? region : NULL;
}

static const al_acoustic_portal_hint_t *AL_FindAcousticPortalHint(int from_area, int to_area)
{
    for (size_t i = 0; i < s_num_acoustic_portal_hints; i++) {
        const al_acoustic_portal_hint_t *hint = &s_acoustic_portal_hints[i];
        if (hint->from_area == from_area && hint->to_area == to_area)
            return hint;
    }

    return NULL;
}

static void AL_AddAcousticPortalHintEdge(int from_area, int to_area,
                                         float openness, float transmission,
                                         float gainhf)
{
    al_acoustic_region_t *from_region = AL_MutableAcousticRegionByArea(from_area);
    al_acoustic_region_t *to_region = AL_MutableAcousticRegionByArea(to_area);
    if (!from_region || !to_region || from_area == to_area)
        return;

    AL_AddAcousticRegionNeighbor(from_region, to_area);
    from_region->portal_openness = max(from_region->portal_openness, openness);
    to_region->portal_openness = max(to_region->portal_openness, openness);

    for (size_t i = 0; i < s_num_acoustic_portal_hints; i++) {
        al_acoustic_portal_hint_t *hint = &s_acoustic_portal_hints[i];
        if (hint->from_area == from_area && hint->to_area == to_area) {
            hint->openness = Q_clipf(openness, 0.0f, 1.0f);
            hint->transmission = Q_clipf(transmission, 0.0f, 1.0f);
            hint->gainhf = Q_clipf(gainhf, 0.05f, 1.0f);
            return;
        }
    }

    size_t new_count = s_num_acoustic_portal_hints + 1;
    s_acoustic_portal_hints = static_cast<al_acoustic_portal_hint_t *>(
        Z_Realloc(s_acoustic_portal_hints, sizeof(*s_acoustic_portal_hints) * new_count));

    al_acoustic_portal_hint_t *hint = &s_acoustic_portal_hints[s_num_acoustic_portal_hints];
    hint->from_area = from_area;
    hint->to_area = to_area;
    hint->openness = Q_clipf(openness, 0.0f, 1.0f);
    hint->transmission = Q_clipf(transmission, 0.0f, 1.0f);
    hint->gainhf = Q_clipf(gainhf, 0.05f, 1.0f);
    s_num_acoustic_portal_hints = new_count;
}

static void AL_AddAcousticPortalHint(int from_area, int to_area, float openness,
                                     float transmission, float gainhf,
                                     bool bidirectional)
{
    openness = Q_clipf(openness, 0.0f, 1.0f);
    transmission = Q_clipf(transmission, 0.0f, 1.0f);
    gainhf = Q_clipf(gainhf, 0.05f, 1.0f);

    AL_AddAcousticPortalHintEdge(from_area, to_area, openness, transmission, gainhf);
    if (bidirectional)
        AL_AddAcousticPortalHintEdge(to_area, from_area, openness, transmission, gainhf);
}

static bool AL_AcousticAreaReachable(int area)
{
    if (area < 0 || !cl.bsp || area >= cl.bsp->numareas)
        return false;

    if (!cl.frame.areabytes)
        return true;

    int byte_index = area >> 3;
    if (byte_index < 0 || byte_index >= cl.frame.areabytes)
        return false;

    return Q_IsBitSet(cl.frame.areabits, area);
}

static bool AL_AcousticRegionsAreNeighbors(const al_acoustic_region_t *a,
                                           const al_acoustic_region_t *b)
{
    if (!a || !b)
        return false;

    for (size_t i = 0; i < a->num_portal_neighbors; i++) {
        if (a->portal_neighbors[i] == b->area)
            return true;
    }

    return false;
}

static void AL_AcousticRegionCenter(const al_acoustic_region_t *region, vec3_t out)
{
    if (!region || !region->valid) {
        VectorCopy(listener_origin, out);
        return;
    }

    VectorAvg(region->mins, region->maxs, out);
}

static void AL_ClampPointToRegion(const vec3_t point, const al_acoustic_region_t *region, vec3_t out)
{
    if (!region || !region->valid) {
        VectorCopy(point, out);
        return;
    }

    out[0] = Q_clipf(point[0], region->mins[0], region->maxs[0]);
    out[1] = Q_clipf(point[1], region->mins[1], region->maxs[1]);
    out[2] = Q_clipf(point[2], region->mins[2], region->maxs[2]);
}

static void AL_EstimatePortalPoint(const al_acoustic_region_t *a,
                                   const al_acoustic_region_t *b,
                                   vec3_t out)
{
    vec3_t ca, cb, pa, pb;

    AL_AcousticRegionCenter(a, ca);
    AL_AcousticRegionCenter(b, cb);
    AL_ClampPointToRegion(cb, a, pa);
    AL_ClampPointToRegion(ca, b, pb);
    VectorAvg(pa, pb, out);
}

static float AL_AcousticGroupPortalTransmission(al_acoustic_group_t group)
{
    switch (group) {
    case AL_ACOUSTIC_GROUP_SKY:
        return 1.0f;
    case AL_ACOUSTIC_GROUP_GRATE:
        return 0.95f;
    case AL_ACOUSTIC_GROUP_STONE:
        return 0.88f;
    case AL_ACOUSTIC_GROUP_GLASS:
        return 0.85f;
    case AL_ACOUSTIC_GROUP_WOOD:
        return 0.82f;
    case AL_ACOUSTIC_GROUP_METAL:
        return 0.80f;
    case AL_ACOUSTIC_GROUP_FOLIAGE:
        return 0.78f;
    case AL_ACOUSTIC_GROUP_SOFT:
        return 0.75f;
    case AL_ACOUSTIC_GROUP_WATER:
        return 0.60f;
    default:
        return 0.85f;
    }
}

static float AL_AcousticRegionMaterialTransmission(const al_acoustic_region_t *region)
{
    if (!region || region->group_weight_total <= 0.0f)
        return 0.85f;

    float weighted = 0.0f;
    for (int i = 0; i < AL_ACOUSTIC_GROUP_COUNT; i++) {
        weighted += region->group_weights[i] *
                    AL_AcousticGroupPortalTransmission(static_cast<al_acoustic_group_t>(i));
    }

    return Q_clipf(weighted / region->group_weight_total, 0.45f, 1.0f);
}

static float AL_AcousticRegionGainHF(const al_acoustic_region_t *region)
{
    if (!region || region->group_weight_total <= 0.0f)
        return 1.0f;

    float weighted = 0.0f;
    for (int i = 0; i < AL_ACOUSTIC_GROUP_COUNT; i++) {
        weighted += region->group_weights[i] *
                    AL_AcousticGroupSendGainHF(static_cast<al_acoustic_group_t>(i));
    }

    return Q_clipf(weighted / region->group_weight_total, 0.35f, 1.0f);
}

static float AL_PortalAperturePenalty(const al_acoustic_region_t *a,
                                      const al_acoustic_region_t *b)
{
    float openness = 0.0f;
    if (a && b) {
        const al_acoustic_portal_hint_t *hint =
            AL_FindAcousticPortalHint(a->area, b->area);
        if (hint)
            openness = max(openness, hint->openness);
    }
    if (a)
        openness = max(openness, a->portal_openness);
    if (b)
        openness = max(openness, b->portal_openness);

    return Q_clipf(0.58f + openness * 0.34f, 0.55f, 0.92f);
}

static void AL_ApplyPortalHintEdge(const al_acoustic_region_t *from,
                                   const al_acoustic_region_t *to,
                                   float *transmission, float *gainhf)
{
    if (!from || !to)
        return;

    const al_acoustic_portal_hint_t *hint =
        AL_FindAcousticPortalHint(from->area, to->area);
    if (!hint)
        return;

    if (transmission)
        *transmission *= hint->transmission;
    if (gainhf)
        *gainhf *= hint->gainhf;
}

static float AL_PortalBendPenalty(const vec3_t a, const vec3_t b, const vec3_t c)
{
    vec3_t in, out;
    VectorSubtract(b, a, in);
    VectorSubtract(c, b, out);

    if (VectorNormalize(in) <= 1.0f || VectorNormalize(out) <= 1.0f)
        return 1.0f;

    float dot = Q_clipf(DotProduct(in, out), -1.0f, 1.0f);
    return Q_clipf(0.70f + 0.30f * max(dot, 0.0f), 0.55f, 1.0f);
}

static void AL_EvaluatePortalRoute(const vec3_t source_origin,
                                   const al_acoustic_region_t *listener_region,
                                   const al_acoustic_region_t *mid_region,
                                   const al_acoustic_region_t *source_region,
                                   al_portal_path_t *out_path)
{
    vec3_t waypoints[2];
    int hops = mid_region ? 2 : 1;

    AL_EstimatePortalPoint(listener_region, mid_region ? mid_region : source_region, waypoints[0]);
    if (mid_region)
        AL_EstimatePortalPoint(mid_region, source_region, waypoints[1]);

    float direct_distance = max(VectorDistance(listener_origin, source_origin), 1.0f);
    float route_distance = VectorDistance(listener_origin, waypoints[0]);
    if (mid_region)
        route_distance += VectorDistance(waypoints[0], waypoints[1]);
    route_distance += VectorDistance(mid_region ? waypoints[1] : waypoints[0], source_origin);
    route_distance = max(route_distance, direct_distance);

    float length_factor = Q_clipf(direct_distance / route_distance, 0.35f, 1.0f);
    float aperture = AL_PortalAperturePenalty(listener_region, mid_region ? mid_region : source_region);
    if (mid_region)
        aperture *= AL_PortalAperturePenalty(mid_region, source_region);

    float bend = AL_PortalBendPenalty(listener_origin, waypoints[0],
                                      mid_region ? waypoints[1] : source_origin);
    if (mid_region)
        bend *= AL_PortalBendPenalty(waypoints[0], waypoints[1], source_origin);

    float material = (AL_AcousticRegionMaterialTransmission(listener_region) +
                      AL_AcousticRegionMaterialTransmission(source_region)) * 0.5f;
    if (mid_region)
        material = (material + AL_AcousticRegionMaterialTransmission(mid_region)) * 0.5f;

    float material_gainhf = (AL_AcousticRegionGainHF(listener_region) +
                             AL_AcousticRegionGainHF(source_region)) * 0.5f;
    if (mid_region)
        material_gainhf = (material_gainhf + AL_AcousticRegionGainHF(mid_region)) * 0.5f;

    float hint_transmission = 1.0f;
    float hint_gainhf = 1.0f;
    AL_ApplyPortalHintEdge(listener_region, mid_region ? mid_region : source_region,
                           &hint_transmission, &hint_gainhf);
    if (mid_region)
        AL_ApplyPortalHintEdge(mid_region, source_region, &hint_transmission, &hint_gainhf);

    float transmission = Q_clipf(length_factor * aperture * bend * material *
                                     hint_transmission,
                                 0.0f, 1.0f);
    material_gainhf = Q_clipf(material_gainhf * hint_gainhf, 0.25f, 1.0f);

    out_path->valid = transmission > 0.05f;
    out_path->hops = hops;
    out_path->transmission = transmission;
    out_path->occlusion = Q_clipf(1.0f - transmission, 0.0f, 0.95f);
    out_path->send_scale = Q_clipf(1.0f + (1.0f - transmission) * 0.55f +
                                       static_cast<float>(hops) * 0.12f,
                                   1.0f, 1.75f);
    out_path->send_gainhf = Q_clipf((0.35f + 0.65f * transmission) * material_gainhf,
                                    0.25f, 1.0f);
    out_path->reverb_gainhf = out_path->send_gainhf;
    out_path->cutoff_hz = Q_clipf(out_path->send_gainhf * S_OCCLUSION_CUTOFF_REFERENCE_HZ,
                                  S_OCCLUSION_CUTOFF_MIN_HZ,
                                  S_OCCLUSION_CUTOFF_CLEAR_HZ);
}

static bool AL_ResolvePortalPropagation(const vec3_t source_origin,
                                        const al_acoustic_space_t *listener_space,
                                        const al_acoustic_space_t *source_space,
                                        al_portal_path_t *out_path)
{
    memset(out_path, 0, sizeof(*out_path));

    if (!s_acoustic_regions || !cl.bsp)
        return false;

    if (!listener_space || !source_space || listener_space->area < 0 ||
        !source_space->valid || source_space->area < 0 ||
        source_space->area == listener_space->area ||
        !AL_AcousticAreaReachable(source_space->area))
        return false;

    const al_acoustic_region_t *listener_region = listener_space->region;
    const al_acoustic_region_t *source_region = source_space->region;
    if (!listener_region || !source_region)
        return false;

    al_portal_path_t candidate{};
    if (AL_AcousticRegionsAreNeighbors(listener_region, source_region)) {
        AL_EvaluatePortalRoute(source_origin, listener_region, NULL, source_region, &candidate);
        if (candidate.valid)
            *out_path = candidate;
    }

    for (size_t i = 0; i < listener_region->num_portal_neighbors; i++) {
        int mid_area = listener_region->portal_neighbors[i];
        if (mid_area == listener_region->area || mid_area == source_region->area ||
            !AL_AcousticAreaReachable(mid_area))
            continue;

        const al_acoustic_region_t *mid_region = AL_AcousticRegionByArea(mid_area);
        if (!mid_region || !AL_AcousticRegionsAreNeighbors(mid_region, source_region))
            continue;

        AL_EvaluatePortalRoute(source_origin, listener_region, mid_region, source_region, &candidate);
        if (candidate.valid && (!out_path->valid ||
                                candidate.transmission > out_path->transmission)) {
            *out_path = candidate;
        }
    }

    return out_path->valid;
}

static float AL_SourcePathCutoffForGainHF(float gainhf)
{
    return Q_clipf(gainhf * S_OCCLUSION_CUTOFF_REFERENCE_HZ,
                   S_OCCLUSION_CUTOFF_MIN_HZ,
                   S_OCCLUSION_CUTOFF_CLEAR_HZ);
}

static void AL_InitSourcePathState(al_source_path_state_t *path)
{
    memset(path, 0, sizeof(*path));
    path->kind = AL_SOURCE_PATH_LOCAL;
    path->occlusion_floor = 0.0f;
    path->cutoff_ceiling = S_OCCLUSION_CUTOFF_CLEAR_HZ;
    path->reverb_gainhf_ceiling = 1.0f;
    path->send_scale = 1.0f;
    path->send_gainhf = 1.0f;
}

static void AL_SetSourcePathDamping(al_source_path_state_t *path, float occlusion_floor,
                                    float gainhf_ceiling, float send_scale, float send_gainhf)
{
    path->occlusion_floor = max(path->occlusion_floor, occlusion_floor);
    path->cutoff_ceiling = min(path->cutoff_ceiling, AL_SourcePathCutoffForGainHF(gainhf_ceiling));
    path->reverb_gainhf_ceiling = min(path->reverb_gainhf_ceiling, gainhf_ceiling);
    path->send_scale *= send_scale;
    path->send_gainhf *= send_gainhf;
}

static bool AL_ResolveSourcePathState(const vec3_t source_origin, bool fullvolume,
                                      al_source_path_state_t *path)
{
    AL_InitSourcePathState(path);

    if (fullvolume || !s_acoustic_regions || !cl.bsp)
        return false;

    al_acoustic_space_t *listener_space = AL_GetListenerAcousticSpaceForSend();
    if (!listener_space || listener_space->area < 0)
        return false;

    AL_ClassifyAcousticSpace(source_origin, false, &path->source_space);
    if (!path->source_space.valid || path->source_space.area < 0)
        return false;

    path->listener_space = listener_space;

    const bool same_area = path->source_space.area == listener_space->area;
    const bool source_exterior = path->source_space.exterior;
    const bool listener_exterior = listener_space->exterior;
    const bool exterior_mismatch = source_exterior != listener_exterior;
    const bool reachable = AL_AcousticAreaReachable(path->source_space.area);

    if (same_area) {
        path->kind = AL_SOURCE_PATH_SAME_SPACE;
        path->send_scale = 0.70f;
        path->send_gainhf = 1.0f;
        return true;
    }

    if (!reachable) {
        path->kind = AL_SOURCE_PATH_UNREACHABLE;
        AL_SetSourcePathDamping(path, 0.92f, 0.18f, 0.65f, 0.35f);
        return true;
    }

    if (exterior_mismatch) {
        if (source_exterior) {
            path->kind = AL_SOURCE_PATH_EXTERIOR_TO_INTERIOR;
            AL_SetSourcePathDamping(path, 0.22f, 0.62f, 1.35f, 0.68f);
        } else {
            path->kind = AL_SOURCE_PATH_INTERIOR_TO_EXTERIOR;
            AL_SetSourcePathDamping(path, 0.16f, 0.72f, 1.18f, 0.78f);
        }
    } else if (AL_AcousticRegionsAreNeighbors(listener_space->region, path->source_space.region)) {
        path->kind = AL_SOURCE_PATH_ADJACENT_SPACE;
        AL_SetSourcePathDamping(path, 0.12f, 0.86f, 1.14f, 0.90f);
    } else {
        path->kind = AL_SOURCE_PATH_CROSS_SPACE;
        AL_SetSourcePathDamping(path, 0.20f, 0.76f, 1.25f, 0.82f);
    }

    if (path->source_space.dominant_group == AL_ACOUSTIC_GROUP_WATER)
        AL_SetSourcePathDamping(path, 0.20f, 0.70f, 1.10f,
                                AL_AcousticGroupSendGainHF(path->source_space.dominant_group));
    else
        path->send_gainhf *= AL_AcousticGroupSendGainHF(path->source_space.dominant_group);

    if (AL_ResolvePortalPropagation(source_origin, listener_space, &path->source_space,
                                    &path->portal_path)) {
        path->kind = AL_SOURCE_PATH_PORTAL;
        path->send_scale *= path->portal_path.send_scale;
        path->send_gainhf *= path->portal_path.send_gainhf;
    }

    path->send_scale = Q_clipf(path->send_scale, 0.45f, 1.85f);
    path->send_gainhf = Q_clipf(path->send_gainhf, 0.20f, 1.0f);
    return true;
}

static bool AL_ApplySourcePathState(const vec3_t source_origin, bool fullvolume,
                                    float *occlusion, float *cutoff_hz,
                                    float *reverb_gainhf, float *send_scale,
                                    float *send_gainhf)
{
    al_source_path_state_t path;
    if (!AL_ResolveSourcePathState(source_origin, fullvolume, &path))
        return false;

    if (path.portal_path.valid) {
        if (occlusion && *occlusion > path.portal_path.occlusion)
            *occlusion = path.portal_path.occlusion;
        if (cutoff_hz && *cutoff_hz < path.portal_path.cutoff_hz)
            *cutoff_hz = path.portal_path.cutoff_hz;
        if (reverb_gainhf && *reverb_gainhf < path.portal_path.reverb_gainhf)
            *reverb_gainhf = path.portal_path.reverb_gainhf;
    }

    const bool direct_path_clear = occlusion && *occlusion <= S_OCCLUSION_CLEAR_MARGIN;
    if (!direct_path_clear) {
        if (occlusion && *occlusion < path.occlusion_floor)
            *occlusion = path.occlusion_floor;
        if (cutoff_hz && *cutoff_hz > path.cutoff_ceiling)
            *cutoff_hz = path.cutoff_ceiling;
        if (reverb_gainhf && *reverb_gainhf > path.reverb_gainhf_ceiling)
            *reverb_gainhf = path.reverb_gainhf_ceiling;
    }
    if (send_scale)
        *send_scale = Q_clipf(*send_scale * path.send_scale, 0.0f, 1.85f);
    if (send_gainhf)
        *send_gainhf = Q_clipf(*send_gainhf * path.send_gainhf, 0.20f, 1.0f);

    return true;
}

static void AL_UpdateReverbSend(channel_t *ch, float distance, float occlusion_mix,
                                float region_send_scale, float region_send_gainhf)
{
    if (!s_reverb_slot || !al_reverb || !al_reverb->integer) {
        qalSource3i(ch->srcnum, AL_AUXILIARY_SEND_FILTER, AL_EFFECT_NULL, 0, AL_FILTER_NULL);
        return;
    }

    if (!s_reverb_filters || !al_reverb_send || !al_reverb_send->integer) {
        qalSource3i(ch->srcnum, AL_AUXILIARY_SEND_FILTER, s_reverb_slot, 0, AL_FILTER_NULL);
        return;
    }

    int ch_index = static_cast<int>(ch - s_channels);
    if (ch_index < 0 || ch_index >= s_num_reverb_filters) {
        qalSource3i(ch->srcnum, AL_AUXILIARY_SEND_FILTER, s_reverb_slot, 0, AL_FILTER_NULL);
        return;
    }

    float send = AL_ComputeReverbSend(distance, occlusion_mix);
    send = Q_clipf(send * region_send_scale, 0.0f, 1.0f);
    float send_gainhf = Q_clipf(send * region_send_gainhf, 0.0f, 1.0f);
    if (occlusion_mix > 0.0f) {
        float material_gainhf = ch->occlusion_reverb_gainhf > 0.0f ? ch->occlusion_reverb_gainhf : 1.0f;
        send_gainhf = Q_clipf(send_gainhf * FASTLERP(1.0f, material_gainhf, occlusion_mix), 0.0f, 1.0f);
    }
    qalFilterf(s_reverb_filters[ch_index], AL_LOWPASS_GAIN, send);
    qalFilterf(s_reverb_filters[ch_index], AL_LOWPASS_GAINHF, send_gainhf);
    qalSource3i(ch->srcnum, AL_AUXILIARY_SEND_FILTER, s_reverb_slot, 0, s_reverb_filters[ch_index]);
}

static void AL_UpdateSpatialEffects(channel_t *ch, const vec3_t origin, bool fullvolume)
{
    float distance = fullvolume ? 0.0f : AL_DistanceFromListener(origin);
    float occlusion = 0.0f;

    if (!fullvolume && ch->dist_mult > 0.0f && s_occlusion && s_occlusion->integer) {
        occlusion = S_GetOcclusion(ch, origin);
    } else {
        S_ResetOcclusion(ch);
    }

    float region_send_scale = 1.0f;
    float region_send_gainhf = 1.0f;
    bool apply_direct_path = !fullvolume && ch->dist_mult > 0.0f &&
                             s_occlusion && s_occlusion->integer;
    if (ch->dist_mult > 0.0f)
        AL_ApplySourcePathState(origin, fullvolume,
                                apply_direct_path ? &occlusion : NULL,
                                apply_direct_path ? &ch->occlusion_cutoff : NULL,
                                apply_direct_path ? &ch->occlusion_reverb_gainhf : NULL,
                                &region_send_scale, &region_send_gainhf);

    float occlusion_mix = S_MapOcclusion(occlusion);
    float air_absorption = 0.0f;
    if (!s_underwater_flag)
        air_absorption = AL_ComputeAirAbsorption(distance);

    AL_UpdateDirectFilter(ch, occlusion_mix, air_absorption, true);
    AL_UpdateReverbSend(ch, distance, occlusion_mix, region_send_scale, region_send_gainhf);
}

static bool AL_Init(void)
{
    int i;

    i = QAL_Init();
    if (i < 0)
        goto fail0;
    s_merge_looping_minval = i + 1;

    Com_DPrintf("AL_VENDOR: %s\n", qalGetString(AL_VENDOR));
    Com_DPrintf("AL_RENDERER: %s\n", qalGetString(AL_RENDERER));
    Com_DPrintf("AL_VERSION: %s\n", qalGetString(AL_VERSION));
    Com_DDPrintf("AL_EXTENSIONS: %s\n", qalGetString(AL_EXTENSIONS));

    // generate source names
    qalGetError();
    qalGenSources(1, &s_stream);

    s_srcnums = static_cast<ALuint *>(Z_TagMalloc(sizeof(*s_srcnums) * s_maxchannels, TAG_SOUND));

    for (i = 0; i < s_maxchannels; i++) {
        qalGenSources(1, &s_srcnums[i]);
        if (qalGetError() != AL_NO_ERROR) {
            break;
        }
        s_numalsources++;
    }

    if (s_numalsources != s_maxchannels)
        s_srcnums = static_cast<ALuint *>(Z_Realloc(s_srcnums, sizeof(*s_srcnums) * s_numalsources));

    Com_DPrintf("Got %d AL sources\n", i);

    if (i < MIN_CHANNELS) {
        Com_SetLastError("Insufficient number of AL sources");
        goto fail1;
    }

    s_numchannels = i;

    s_volume->changed = s_volume_changed;
    s_volume_changed(s_volume);

    al_merge_looping = Cvar_Get("al_merge_looping", "1", 0);
    al_merge_looping->changed = al_merge_looping_changed;

    al_distance_model = Cvar_Get("al_distance_model", "1", 0);
    al_distance_model->changed = al_distance_model_changed;
    al_distance_model_changed(al_distance_model);

    s_loop_points = qalIsExtensionPresent("AL_SOFT_loop_points");
    s_source_spatialize = qalIsExtensionPresent("AL_SOFT_source_spatialize");
    s_supports_float = qalIsExtensionPresent("AL_EXT_float32");

    s_air_absorption_enum = qalGetEnumValue("AL_AIR_ABSORPTION_FACTOR");
    s_air_absorption_supported = (s_air_absorption_enum != 0);

    // init stream source
    qalSourcef(s_stream, AL_ROLLOFF_FACTOR, 0.0f);
    qalSourcei(s_stream, AL_SOURCE_RELATIVE, AL_TRUE);
    if (s_source_spatialize)
        qalSourcei(s_stream, AL_SOURCE_SPATIALIZE_SOFT, AL_FALSE);

    if (qalIsExtensionPresent("AL_SOFT_direct_channels_remix"))
        qalSourcei(s_stream, AL_DIRECT_CHANNELS_SOFT, AL_REMIX_UNMATCHED_SOFT);
    else if (qalIsExtensionPresent("AL_SOFT_direct_channels"))
        qalSourcei(s_stream, AL_DIRECT_CHANNELS_SOFT, AL_TRUE);

    // init underwater and occlusion filters
    if (qalGenFilters && qalGetEnumValue("AL_FILTER_LOWPASS")) {
        qalGenFilters(1, &s_underwater_filter);
        qalFilteri(s_underwater_filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
        s_underwater_gain_hf->changed = s_underwater_gain_hf_changed;
        s_underwater_gain_hf_changed(s_underwater_gain_hf);

        s_num_occlusion_filters = 0;
        if (s_numchannels > 0) {
            s_occlusion_filters = static_cast<ALuint *>(
                Z_TagMalloc(sizeof(*s_occlusion_filters) * s_numchannels, TAG_SOUND));

            qalGetError();
            for (i = 0; i < s_numchannels; i++) {
                qalGenFilters(1, &s_occlusion_filters[i]);
                if (qalGetError() != AL_NO_ERROR)
                    break;
                qalFilteri(s_occlusion_filters[i], AL_FILTER_TYPE, AL_FILTER_LOWPASS);
                qalFilterf(s_occlusion_filters[i], AL_LOWPASS_GAIN, 1.0f);
                qalFilterf(s_occlusion_filters[i], AL_LOWPASS_GAINHF, 1.0f);
                s_num_occlusion_filters++;
            }

            if (!s_num_occlusion_filters) {
                Z_Free(s_occlusion_filters);
                s_occlusion_filters = NULL;
            } else if (s_num_occlusion_filters < s_numchannels) {
                s_occlusion_filters = static_cast<ALuint *>(
                    Z_Realloc(s_occlusion_filters,
                              sizeof(*s_occlusion_filters) * s_num_occlusion_filters));
            }
        }
    }

    s_eax_effect_available = false;
    if (qalGenEffects && qalGenAuxiliaryEffectSlots) {
        qalGenEffects(1, &s_reverb_effect);
        qalGenAuxiliaryEffectSlots(1, &s_reverb_slot);
        if (!qalGetError()) {
            qalGetError();
            qalEffecti(s_reverb_effect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);
            s_eax_effect_available = (qalGetError() == AL_NO_ERROR);
            if (!s_eax_effect_available) {
                qalGetError();
                qalEffecti(s_reverb_effect, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
                qalGetError();
            }
        }
    }

    if (s_reverb_slot && qalGenFilters && qalGetEnumValue("AL_FILTER_LOWPASS")) {
        s_num_reverb_filters = 0;
        if (s_numchannels > 0) {
            s_reverb_filters = static_cast<ALuint *>(
                Z_TagMalloc(sizeof(*s_reverb_filters) * s_numchannels, TAG_SOUND));

            qalGetError();
            for (i = 0; i < s_numchannels; i++) {
                qalGenFilters(1, &s_reverb_filters[i]);
                if (qalGetError() != AL_NO_ERROR)
                    break;
                qalFilteri(s_reverb_filters[i], AL_FILTER_TYPE, AL_FILTER_LOWPASS);
                qalFilterf(s_reverb_filters[i], AL_LOWPASS_GAIN, 1.0f);
                qalFilterf(s_reverb_filters[i], AL_LOWPASS_GAINHF, 1.0f);
                s_num_reverb_filters++;
            }

            if (!s_num_reverb_filters) {
                Z_Free(s_reverb_filters);
                s_reverb_filters = NULL;
            } else if (s_num_reverb_filters < s_numchannels) {
                s_reverb_filters = static_cast<ALuint *>(
                    Z_Realloc(s_reverb_filters, sizeof(*s_reverb_filters) * s_num_reverb_filters));
            }
        }
    }

    al_reverb = Cvar_Get("al_reverb", "1", CVAR_ARCHIVE);
    al_reverb->changed = al_reverb_changed;
    al_reverb_lerp_time = Cvar_Get("al_reverb_lerp_time", "3.0", CVAR_ARCHIVE);
    al_reverb_send = Cvar_Get("al_reverb_send", "1", CVAR_ARCHIVE);
    al_reverb_send_distance = Cvar_Get("al_reverb_send_distance", "2048", CVAR_ARCHIVE);
    al_reverb_send_min = Cvar_Get("al_reverb_send_min", "0.2", CVAR_ARCHIVE);
    al_reverb_send_occlusion_boost = Cvar_Get("al_reverb_send_occlusion_boost", "1.5", CVAR_ARCHIVE);
    al_eax = Cvar_Get("al_eax", "1", CVAR_ARCHIVE);
    al_eax_lerp_time = Cvar_Get("al_eax_lerp_time", "1.0", CVAR_ARCHIVE);

    al_timescale = Cvar_Get("al_timescale", "1", 0);
    al_air_absorption = Cvar_Get("al_air_absorption", "1", CVAR_ARCHIVE);
    al_air_absorption_distance = Cvar_Get("al_air_absorption_distance", "2048", CVAR_ARCHIVE);
    al_doppler = Cvar_Get("al_doppler", "1", 0);
    al_doppler_speed = Cvar_Get("al_doppler_speed", "13500", 0);
    al_doppler_min_speed = Cvar_Get("al_doppler_min_speed", "30", 0);
    al_doppler_max_speed = Cvar_Get("al_doppler_max_speed", "4000", 0);
    al_doppler_smooth = Cvar_Get("al_doppler_smooth", "12", 0);
    al_doppler->changed = al_doppler_changed;
    al_doppler_speed->changed = al_doppler_changed;
    al_doppler_changed(al_doppler);

    AL_LoadReverbEnvironments();
    AL_LoadEAXEffectProfiles();
    AL_ClearEAXZones();

    Com_Printf("OpenAL initialized.\n");
    return true;

fail1:
    QAL_Shutdown();
fail0:
    Com_EPrintf("Failed to initialize OpenAL: %s\n", Com_GetLastError());
    return false;
}

static void AL_Shutdown(void)
{
    Com_Printf("Shutting down OpenAL.\n");

    if (s_numchannels) {
        // delete source names
        qalDeleteSources(s_numchannels, s_srcnums);
        Z_Free(s_srcnums);
        s_srcnums = NULL;
        s_numalsources = 0;
        s_numchannels = 0;
    }

    if (s_stream) {
        AL_StreamStop();
        qalDeleteSources(1, &s_stream);
        s_stream = 0;
    }

    if (s_underwater_filter) {
        qalDeleteFilters(1, &s_underwater_filter);
        s_underwater_filter = 0;
    }
    if (s_occlusion_filters) {
        qalDeleteFilters(s_num_occlusion_filters, s_occlusion_filters);
        Z_Free(s_occlusion_filters);
        s_occlusion_filters = NULL;
        s_num_occlusion_filters = 0;
    }
    if (s_reverb_filters) {
        qalDeleteFilters(s_num_reverb_filters, s_reverb_filters);
        Z_Free(s_reverb_filters);
        s_reverb_filters = NULL;
        s_num_reverb_filters = 0;
    }

    if (s_reverb_effect) {
        qalDeleteEffects(1, &s_reverb_effect);
        s_reverb_effect = 0;
    }

    if (s_reverb_slot) {
        qalDeleteAuxiliaryEffectSlots(1, &s_reverb_slot);
        s_reverb_slot = 0;
    }
    s_eax_effect_available = false;

    AL_FreeReverbEnvironments(s_reverb_environments, s_num_reverb_environments);
    s_reverb_environments = NULL;
    s_num_reverb_environments = 0;
    AL_FreeAcousticRegions();
    AL_ClearEAXZones();

    s_underwater_flag = false;
    s_underwater_gain_hf->changed = NULL;
    s_volume->changed = NULL;
    al_merge_looping->changed = NULL;
    if (al_distance_model)
        al_distance_model->changed = NULL;
    al_doppler->changed = NULL;
    if (al_doppler_speed)
        al_doppler_speed->changed = NULL;

    QAL_Shutdown();
}

static ALenum AL_GetSampleFormat(int width, int channels)
{
    if (channels < 1 || channels > 2)
        return 0;

    switch (width) {
    case 1:
        return AL_FORMAT_MONO8 + (channels - 1) * 2;
    case 2:
        return AL_FORMAT_MONO16 + (channels - 1) * 2;
    case 4:
        if (!s_supports_float)
            return 0;
        return AL_FORMAT_MONO_FLOAT32 + (channels - 1);
    default:
        return 0;
    }
}

static sfxcache_t *AL_UploadSfx(sfx_t *s)
{
    ALsizei size = s_info.samples * s_info.width * s_info.channels;
    ALenum format = AL_GetSampleFormat(s_info.width, s_info.channels);
    ALuint buffer = 0;
    sfxcache_t *sc = nullptr;

    if (!format) {
        Com_SetLastError("Unsupported sample format");
        goto fail;
    }

    qalGetError();
    qalGenBuffers(1, &buffer);
    if (qalGetError()) {
        Com_SetLastError("Failed to generate buffer");
        goto fail;
    }

    qalBufferData(buffer, format, s_info.data, size, s_info.rate);
    if (qalGetError()) {
        Com_SetLastError("Failed to upload samples");
        qalDeleteBuffers(1, &buffer);
        goto fail;
    }

    // specify OpenAL-Soft style loop points
    if (s_info.loopstart > 0 && s_loop_points) {
        ALint points[2] = { s_info.loopstart, s_info.samples };
        qalBufferiv(buffer, AL_LOOP_POINTS_SOFT, points);
    }

    // allocate placeholder sfxcache
    sc = s->cache = static_cast<sfxcache_t *>(S_Malloc(sizeof(*sc)));
    sc->length = s_info.samples * 1000LL / s_info.rate; // in msec
    sc->loopstart = s_info.loopstart;
    sc->width = s_info.width;
    sc->channels = s_info.channels;
    sc->size = size;
    sc->bufnum = buffer;

    return sc;

fail:
    s->error = Q_ERR_LIBRARY_ERROR;
    return NULL;
}

static void AL_DeleteSfx(sfx_t *s)
{
    sfxcache_t *sc = s->cache;
    if (sc) {
        ALuint name = sc->bufnum;
        qalDeleteBuffers(1, &name);
    }
}

static int AL_GetBeginofs(float timeofs)
{
    return s_paintedtime + timeofs * 1000;
}

static bool AL_DopplerEnabled(void)
{
    return al_doppler && al_doppler->value > 0.0f;
}

static bool AL_EntityHasDoppler(const entity_state_t *state)
{
    if (!state)
        return false;

    if (state->renderfx & RF_DOPPLER)
        return true;

    // Fallback for non-extended clients: projectiles identified by effects.
    const effects_t doppler_fx = EF_ROCKET | EF_BLASTER | EF_BLUEHYPERBLASTER |
                                 EF_HYPERBLASTER | EF_PLASMA | EF_IONRIPPER |
                                 EF_BFG | EF_TRACKER | EF_TRACKERTRAIL |
                                 EF_TRAP;
    return (state->effects & doppler_fx) != 0;
}

static bool AL_GetEntityVelocity(int entnum, vec3_t velocity)
{
    velocity[0] = 0.0f;
    velocity[1] = 0.0f;
    velocity[2] = 0.0f;

    if (!AL_DopplerEnabled())
        return false;

    if (entnum <= 0 || entnum >= (int) cl.csr.max_edicts)
        return false;

    const centity_t *cent = &cl_entities[entnum];
    if (!AL_EntityHasDoppler(&cent->current))
        return false;

    if (cent->serverframe != cl.frame.number)
        return false;

    doppler_state_t *state = &s_doppler_state[entnum];
    int now = cl.time;
    vec3_t origin;
    CL_GetEntitySoundOrigin(entnum, origin);

    if (state->time <= 0) {
        VectorCopy(origin, state->origin);
        VectorClear(state->velocity);
        state->time = now;
        return true;
    }

    int dt_ms = now - state->time;
    if (dt_ms <= 0 || dt_ms > 250) {
        VectorCopy(origin, state->origin);
        VectorClear(state->velocity);
        state->time = now;
        return true;
    }

    float dt = dt_ms * 0.001f;
    vec3_t instant;
    VectorSubtract(origin, state->origin, instant);
    float distance = VectorLength(instant);
    if (distance > AL_DOPPLER_TELEPORT_DISTANCE) {
        VectorCopy(origin, state->origin);
        VectorClear(state->velocity);
        state->time = now;
        return true;
    }
    VectorScale(instant, 1.0f / dt, instant);

    float speed = distance / dt;
    float max_speed = al_doppler_max_speed ? Cvar_ClampValue(al_doppler_max_speed, 0.0f, 20000.0f) : 0.0f;
    if (max_speed > 0.0f && speed > max_speed) {
        float scale = max_speed / speed;
        VectorScale(instant, scale, instant);
        speed = max_speed;
    }

    float min_speed = al_doppler_min_speed ? Cvar_ClampValue(al_doppler_min_speed, 0.0f, max_speed > 0.0f ? max_speed : 20000.0f) : 0.0f;
    if (speed < min_speed)
        VectorClear(instant);

    float smooth = al_doppler_smooth ? Cvar_ClampValue(al_doppler_smooth, 0.0f, 40.0f) : 0.0f;
    if (smooth > 0.0f) {
        float lerp = 1.0f - expf(-smooth * dt);
        state->velocity[0] = FASTLERP(state->velocity[0], instant[0], lerp);
        state->velocity[1] = FASTLERP(state->velocity[1], instant[1], lerp);
        state->velocity[2] = FASTLERP(state->velocity[2], instant[2], lerp);
    } else {
        VectorCopy(instant, state->velocity);
    }

    VectorCopy(origin, state->origin);
    state->time = now;
    VectorCopy(state->velocity, velocity);
    return true;
}

static void AL_UpdateSourceVelocity(channel_t *ch)
{
    vec3_t velocity;
    velocity[0] = 0.0f;
    velocity[1] = 0.0f;
    velocity[2] = 0.0f;

    if (!ch->fixed_origin && !ch->native_binding && !ch->fullvolume)
        AL_GetEntityVelocity(ch->entnum, velocity);

    qalSource3f(ch->srcnum, AL_VELOCITY, AL_UnpackVector(velocity));
}

static void AL_Spatialize(channel_t *ch)
{
    // merged autosounds are handled differently
    if (ch->autosound && al_merge_looping->integer >= s_merge_looping_minval && !ch->no_merge)
        return;

    bool fullvolume = S_IsFullVolume(ch);
    vec3_t origin;

    if (fullvolume) {
        VectorCopy(listener_origin, origin);
    } else if (ch->fixed_origin) {
        VectorCopy(ch->origin, origin);
    } else if (ch->native_binding) {
        if (CL_GetEntitySoundOriginBound(ch->entnum, ch->entity_binding,
                                         ch->origin)) {
            VectorCopy(ch->origin, origin);
        } else {
            /* Keep the last bound position; never retarget a reused slot. */
            VectorCopy(ch->origin, origin);
        }
    } else {
        CL_GetEntitySoundOrigin(ch->entnum, origin);
    }

    if (s_source_spatialize)
        qalSourcei(ch->srcnum, AL_SOURCE_SPATIALIZE_SOFT, !fullvolume);

    qalSourcei(ch->srcnum, AL_SOURCE_RELATIVE, fullvolume);
    if (fullvolume) {
        qalSource3f(ch->srcnum, AL_POSITION, 0, 0, 0);
    } else {
        qalSource3f(ch->srcnum, AL_POSITION, AL_UnpackVector(origin));
    }

    qalSourcef(ch->srcnum, AL_GAIN, ch->master_vol);

    if (al_timescale->integer) {
        qalSourcef(ch->srcnum, AL_PITCH, max(0.75f, CL_Wheel_TimeScale() * Cvar_VariableValue("timescale")));
    } else {
        qalSourcef(ch->srcnum, AL_PITCH, 1.0f);
    }

    AL_UpdateSpatialEffects(ch, origin, fullvolume);
    AL_UpdateSourceVelocity(ch);
    ch->fullvolume = fullvolume;
}

static void AL_StopChannel(channel_t *ch)
{
    if (!ch->sfx)
        return;

#if USE_DEBUG
    if (s_show->integer > 1)
        Com_Printf("%s: %s\n", __func__, ch->sfx->name);
#endif

    // stop it
    qalSourceStop(ch->srcnum);
    qalSourcei(ch->srcnum, AL_BUFFER, AL_NONE);
    memset(ch, 0, sizeof(*ch));
}

/*
=============
AL_PlayChannel
=============
*/
static void AL_PlayChannel(channel_t *ch)
{
    sfxcache_t *sc = ch->sfx->cache;

#if USE_DEBUG
    if (s_show->integer > 1)
        Com_Printf("%s: %s\n", __func__, ch->sfx->name);
#endif

    ch->srcnum = s_srcnums[ch - s_channels];
    qalGetError();
    qalSourcei(ch->srcnum, AL_BUFFER, sc->bufnum);
    qalSourcei(ch->srcnum, AL_LOOPING, ch->autosound || sc->loopstart >= 0);
    qalSourcef(ch->srcnum, AL_GAIN, ch->master_vol);
    qalSourcef(ch->srcnum, AL_REFERENCE_DISTANCE, SOUND_FULLVOLUME);
    qalSourcef(ch->srcnum, AL_MAX_DISTANCE, 8192);
    qalSourcef(ch->srcnum, AL_ROLLOFF_FACTOR, AL_GetRolloffFactor(ch->dist_mult));
    qalSourcei(ch->srcnum, AL_SOURCE_RELATIVE, AL_FALSE);
    if (s_source_spatialize) {
        qalSourcei(ch->srcnum, AL_SOURCE_SPATIALIZE_SOFT, AL_TRUE);
    }

    // force update
    ch->fullvolume = -1;
    AL_Spatialize(ch);

    // play it
    qalSourcePlay(ch->srcnum);
    if (qalGetError() != AL_NO_ERROR) {
        AL_StopChannel(ch);
    } else {
        if (ch->autosound) {
            float offset = fmodf(cls.realtime / 1000.0f, ch->sfx->cache->length / 1000.0f);
            if (ch->no_merge)
                offset = AL_GetLoopSoundPhaseOffsetSeconds(ch, sc);
            qalSourcef(ch->srcnum, AL_SEC_OFFSET, offset);
        }
    }
}

static void AL_IssuePlaysounds(void)
{
    // start any playsounds
    while (1) {
        playsound_t *ps = PS_FIRST(&s_pendingplays);
        if (PS_TERM(ps, &s_pendingplays))
            break;  // no more pending sounds
        if (ps->begin > s_paintedtime)
            break;
        S_IssuePlaysound(ps);
    }
}

static void AL_StopAllSounds(void)
{
    int         i;
    channel_t   *ch;

    for (i = 0, ch = s_channels; i < s_numchannels; i++, ch++) {
        if (!ch->sfx)
            continue;
        AL_StopChannel(ch);
    }

    memset(s_doppler_state, 0, sizeof(s_doppler_state));
    VectorClear(s_doppler_listener_velocity);
}

static channel_t *AL_FindLoopingSound(int entnum, const sfx_t *sfx)
{
    int         i;
    channel_t   *ch;

    for (i = 0, ch = s_channels; i < s_numchannels; i++, ch++) {
        if (!ch->autosound)
            continue;
        // When searching a merge target (entnum == 0), never reuse channels
        // reserved for unmerged doppler sources.
        if (!entnum && ch->no_merge)
            continue;
        if (entnum && ch->entnum != entnum)
            continue;
        if (ch->sfx != sfx)
            continue;
        return ch;
    }

    return NULL;
}

/*
=============
AL_GetLoopGroupGainScale

Keeps dense loop groups from overdriving the OpenAL mixer while preserving
some loudness growth as more emitters join the group.
=============
*/
static float AL_GetLoopGroupGainScale(int count)
{
	if (count <= 1)
		return 1.0f;

	return 1.0f / sqrtf((float)count);
}

/*
=============
AL_GetLoopSoundPhaseOffsetSeconds

Returns a stable per-entity phase offset for unmerged autosounds so identical
projectile loops do not all start in lockstep.
=============
*/
static float AL_GetLoopSoundPhaseOffsetSeconds(const channel_t *ch, const sfxcache_t *sc)
{
	if (!ch || !sc || sc->length <= 0)
		return 0.0f;

	uint32_t hash = 2166136261u;
	hash = (hash ^ (uint32_t)ch->entnum) * 16777619u;
	hash = (hash ^ (uint32_t)sc->bufnum) * 16777619u;

	return (float)(hash % (uint32_t)sc->length) / 1000.0f;
}

/*
=============
AL_AddLoopSoundEntity
=============
*/
static void AL_AddLoopSoundEntity(const entity_state_t *ent, sfx_t *sfx, const sfxcache_t *sc, bool no_merge, float gain_scale)
{
    channel_t *ch = AL_FindLoopingSound(ent->number, sfx);
    if (ch) {
        if (!S_BindChannelEntity(ch)) {
            AL_StopChannel(ch);
            ch = nullptr;
        } else {
            ch->autoframe = s_framecount;
            ch->end = s_paintedtime + sc->length;
            ch->no_merge = no_merge;
            ch->master_vol = S_GetEntityLoopVolume(ent) * gain_scale;
            ch->dist_mult = S_GetEntityLoopDistMult(ent);
            return;
        }
    }

    ch = S_PickChannel(0, 0);
    if (!ch)
        return;

    ch->autosound = true;   // remove next frame
    ch->autoframe = s_framecount;
    ch->no_merge = no_merge;
    ch->sfx = sfx;
    ch->entnum = ent->number;
    if (!S_BindChannelEntity(ch)) {
        memset(ch, 0, sizeof(*ch));
        return;
    }
    ch->master_vol = S_GetEntityLoopVolume(ent) * gain_scale;
    ch->dist_mult = S_GetEntityLoopDistMult(ent);
    ch->end = s_paintedtime + sc->length;

    AL_PlayChannel(ch);
}

/*
=============
AL_MergeLoopSounds
=============
*/
static void AL_MergeLoopSounds(void)
{
    int         i, j;
    int         sounds[MAX_EDICTS];
    float       left, right, left_total, right_total, vol, att, occ_mix;
    float       occlusion_weighted;
    float       occlusion_cutoff_weighted;
    float       occlusion_cutoff_weight;
    float       occlusion_reverb_gainhf_weighted;
    float       occlusion_reverb_gainhf_weight;
    float       region_send_scale_weighted;
    float       region_send_gainhf_weighted;
    float       region_send_weight;
    float       distance_weighted;
    float       gain_total;
    float       pan, pan2, gain;
    channel_t   *ch;
    sfx_t       *sfx;
    sfxcache_t  *sc;
    const entity_state_t *ent;
    vec3_t      origin;
    bool        occlusion_enabled;

    occlusion_enabled = s_occlusion && s_occlusion->integer;

    if (!S_BuildSoundList(sounds))
        return;

    const int entity_count = S_LoopSoundEntityCount();
    for (i = 0; i < entity_count; i++) {
        if (!sounds[i])
            continue;

        const int sound_handle = sounds[i];
        sfx = S_SfxForHandle(cl.sound_precache[sound_handle]);
        if (!sfx)
            continue;       // bad sound effect
        sc = sfx->cache;
        if (!sc)
            continue;

        ent = S_LoopSoundEntity(i);
        if (!ent)
            continue;

        vol = S_GetEntityLoopVolume(ent);
        att = S_GetEntityLoopDistMult(ent);

        bool has_doppler = AL_EntityHasDoppler(ent);
        if (!has_doppler) {
            for (j = i + 1; j < entity_count; j++) {
                if (sounds[j] != sound_handle)
                    continue;
                const entity_state_t *candidate = S_LoopSoundEntity(j);
                if (candidate && AL_EntityHasDoppler(candidate)) {
                    has_doppler = true;
                    break;
                }
            }
        }

        if (has_doppler) {
            int doppler_count = 0;
            for (j = i; j < entity_count; j++) {
                if (sounds[j] != sound_handle)
                    continue;
                const entity_state_t *candidate = S_LoopSoundEntity(j);
                if (candidate && AL_EntityHasDoppler(candidate))
                    doppler_count++;
            }

            float doppler_gain_scale = AL_GetLoopGroupGainScale(doppler_count);
            for (j = i; j < entity_count; j++) {
                if (sounds[j] != sound_handle)
                    continue;
                const entity_state_t *ent_j = S_LoopSoundEntity(j);
                if (!ent_j)
                    continue;
                if (!AL_EntityHasDoppler(ent_j))
                    continue;
                AL_AddLoopSoundEntity(ent_j, sfx, sc, true, doppler_gain_scale);
                sounds[j] = 0;
            }
            if (!sounds[i])
                continue;
        }

        left_total = right_total = 0.0f;
        occlusion_weighted = 0.0f;
        occlusion_cutoff_weighted = 0.0f;
        occlusion_cutoff_weight = 0.0f;
        occlusion_reverb_gainhf_weighted = 0.0f;
        occlusion_reverb_gainhf_weight = 0.0f;
        region_send_scale_weighted = 0.0f;
        region_send_gainhf_weighted = 0.0f;
        region_send_weight = 0.0f;
        distance_weighted = 0.0f;
        gain_total = 0.0f;

        // find the total contribution of all sounds of this type
        CL_GetEntitySoundOrigin(ent->number, origin);
        S_SpatializeOrigin(origin, vol, att,
                           &left, &right,
                           S_GetEntityLoopStereoPan(ent));
        float contribution = left + right;
        float occ = 0.0f;
        float cutoff = S_OCCLUSION_CUTOFF_CLEAR_HZ;
        float reverb_gainhf = 1.0f;
        if (occlusion_enabled && att > 0.0f)
            occ = S_ComputeOcclusion(origin, &cutoff, &reverb_gainhf);
        if (contribution > 0.0f) {
            float source_send_scale = 1.0f;
            float source_send_gainhf = 1.0f;
            AL_ApplySourcePathState(origin, false,
                                    occlusion_enabled ? &occ : NULL,
                                    occlusion_enabled ? &cutoff : NULL,
                                    occlusion_enabled ? &reverb_gainhf : NULL,
                                    &source_send_scale, &source_send_gainhf);
            region_send_scale_weighted += source_send_scale * contribution;
            region_send_gainhf_weighted += source_send_gainhf * contribution;
            region_send_weight += contribution;
        }
        occlusion_weighted += occ * contribution;
        if (occ > 0.0f) {
            occlusion_cutoff_weighted += cutoff * occ * contribution;
            occlusion_cutoff_weight += occ * contribution;
            occlusion_reverb_gainhf_weighted += reverb_gainhf * occ * contribution;
            occlusion_reverb_gainhf_weight += occ * contribution;
        }
        distance_weighted += AL_DistanceFromListener(origin) * contribution;
        gain_total += contribution;
        occ_mix = S_MapOcclusion(occ);
        float occ_gain = FASTLERP(1.0f, S_OCCLUSION_GAIN, occ_mix);
        left *= occ_gain;
        right *= occ_gain;
        left_total += left;
        right_total += right;
        for (j = i + 1; j < entity_count; j++) {
            if (sounds[j] != sound_handle)
                continue;
            sounds[j] = 0;  // don't check this again later

            ent = S_LoopSoundEntity(j);
            if (!ent)
                continue;

            CL_GetEntitySoundOrigin(ent->number, origin);
            float ent_vol = S_GetEntityLoopVolume(ent);
            float ent_att = S_GetEntityLoopDistMult(ent);
            S_SpatializeOrigin(origin,
                               ent_vol,
                               ent_att,
                               &left, &right,
                               S_GetEntityLoopStereoPan(ent));
            contribution = left + right;
            occ = 0.0f;
            cutoff = S_OCCLUSION_CUTOFF_CLEAR_HZ;
            reverb_gainhf = 1.0f;
            if (occlusion_enabled && ent_att > 0.0f)
                occ = S_ComputeOcclusion(origin, &cutoff, &reverb_gainhf);
            if (contribution > 0.0f) {
                float source_send_scale = 1.0f;
                float source_send_gainhf = 1.0f;
                AL_ApplySourcePathState(origin, false,
                                        occlusion_enabled ? &occ : NULL,
                                        occlusion_enabled ? &cutoff : NULL,
                                        occlusion_enabled ? &reverb_gainhf : NULL,
                                        &source_send_scale, &source_send_gainhf);
                region_send_scale_weighted += source_send_scale * contribution;
                region_send_gainhf_weighted += source_send_gainhf * contribution;
                region_send_weight += contribution;
            }
            occlusion_weighted += occ * contribution;
            if (occ > 0.0f) {
                occlusion_cutoff_weighted += cutoff * occ * contribution;
                occlusion_cutoff_weight += occ * contribution;
                occlusion_reverb_gainhf_weighted += reverb_gainhf * occ * contribution;
                occlusion_reverb_gainhf_weight += occ * contribution;
            }
            distance_weighted += AL_DistanceFromListener(origin) * contribution;
            gain_total += contribution;
            occ_mix = S_MapOcclusion(occ);
            occ_gain = FASTLERP(1.0f, S_OCCLUSION_GAIN, occ_mix);
            left *= occ_gain;
            right *= occ_gain;
            left_total += left;
            right_total += right;
        }

        if (left_total == 0 && right_total == 0)
            continue;       // not audible

        left_total = min(1.0f, left_total);
        right_total = min(1.0f, right_total);

        gain = left_total + right_total;
        float occlusion_target = 0.0f;
        if (gain_total > 0.0f)
            occlusion_target = Q_clipf(occlusion_weighted / gain_total, 0.0f, 1.0f);
        float occlusion_cutoff_target = S_OCCLUSION_CUTOFF_CLEAR_HZ;
        if (occlusion_cutoff_weight > 0.0f)
            occlusion_cutoff_target = occlusion_cutoff_weighted / occlusion_cutoff_weight;
        float occlusion_reverb_gainhf_target = 1.0f;
        if (occlusion_reverb_gainhf_weight > 0.0f)
            occlusion_reverb_gainhf_target =
                Q_clipf(occlusion_reverb_gainhf_weighted / occlusion_reverb_gainhf_weight, 0.02f, 1.0f);
        float avg_distance = gain_total > 0.0f ? (distance_weighted / gain_total) : 0.0f;
        float region_send_scale = 1.0f;
        float region_send_gainhf = 1.0f;
        if (region_send_weight > 0.0f) {
            region_send_scale = Q_clipf(region_send_scale_weighted / region_send_weight, 0.75f, 1.5f);
            region_send_gainhf = Q_clipf(region_send_gainhf_weighted / region_send_weight, 0.25f, 1.0f);
        }

        pan  = (right_total - left_total) / (left_total + right_total);
        pan2 = -sqrtf(1.0f - pan * pan);

        ch = AL_FindLoopingSound(0, sfx);
        if (ch) {
            float occlusion = 0.0f;
            if (occlusion_enabled) {
                ch->occlusion_cutoff_target = occlusion_cutoff_target;
                ch->occlusion_reverb_gainhf_target = occlusion_reverb_gainhf_target;
                occlusion = S_SmoothOcclusion(ch, occlusion_target);
            } else {
                S_ResetOcclusion(ch);
            }
            float occlusion_mix = S_MapOcclusion(occlusion);
            float air_absorption = 0.0f;
            if (!s_underwater_flag)
                air_absorption = AL_ComputeAirAbsorption(avg_distance);
            AL_UpdateDirectFilter(ch, occlusion_mix, air_absorption, false);
            AL_UpdateReverbSend(ch, avg_distance, occlusion_mix, region_send_scale, region_send_gainhf);
            qalSourcef(ch->srcnum, AL_GAIN, gain);
            qalSource3f(ch->srcnum, AL_POSITION, pan, 0.0f, pan2);
            qalSource3f(ch->srcnum, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
            ch->autoframe = s_framecount;
            ch->end = s_paintedtime + sc->length;
            ch->no_merge = false;
            continue;
        }

        // allocate a channel
        ch = S_PickChannel(0, 0);
        if (!ch)
            continue;

        ch->srcnum = s_srcnums[ch - s_channels];
        float occlusion = 0.0f;
        if (occlusion_enabled) {
            ch->occlusion_cutoff_target = occlusion_cutoff_target;
            ch->occlusion_reverb_gainhf_target = occlusion_reverb_gainhf_target;
            occlusion = S_SmoothOcclusion(ch, occlusion_target);
        } else {
            S_ResetOcclusion(ch);
        }
        float occlusion_mix = S_MapOcclusion(occlusion);
        float air_absorption = 0.0f;
        if (!s_underwater_flag)
            air_absorption = AL_ComputeAirAbsorption(avg_distance);
        AL_UpdateDirectFilter(ch, occlusion_mix, air_absorption, false);
        AL_UpdateReverbSend(ch, avg_distance, occlusion_mix, region_send_scale, region_send_gainhf);
        qalGetError();
        qalSourcei(ch->srcnum, AL_BUFFER, sc->bufnum);
        qalSourcei(ch->srcnum, AL_LOOPING, AL_TRUE);
        qalSourcei(ch->srcnum, AL_SOURCE_RELATIVE, AL_TRUE);
        if (s_source_spatialize) {
            qalSourcei(ch->srcnum, AL_SOURCE_SPATIALIZE_SOFT, AL_TRUE);
        }
        qalSourcef(ch->srcnum, AL_ROLLOFF_FACTOR, 0.0f);
        qalSourcef(ch->srcnum, AL_GAIN, gain);
        qalSource3f(ch->srcnum, AL_POSITION, pan, 0.0f, pan2);
        qalSource3f(ch->srcnum, AL_VELOCITY, 0.0f, 0.0f, 0.0f);

        ch->autosound = true;   // remove next frame
        ch->autoframe = s_framecount;
        ch->no_merge = false;
        ch->sfx = sfx;
        ch->entnum = ent->number;
        if (!S_BindChannelEntity(ch)) {
            memset(ch, 0, sizeof(*ch));
            continue;
        }
        ch->master_vol = vol;
        ch->dist_mult = att;
        ch->end = s_paintedtime + sc->length;

        // play it
        qalSourcePlay(ch->srcnum);
        if (qalGetError() != AL_NO_ERROR) {
            AL_StopChannel(ch);
        }
    }
}

static void AL_AddLoopSounds(void)
{
    int         i;
    int         sounds[MAX_EDICTS];
    channel_t   *ch;
    sfx_t       *sfx;
    sfxcache_t  *sc;
    const entity_state_t *ent;

    if (cls.state != ca_active || sv_paused->integer || !s_ambient->integer)
        return;

    if (!S_BuildSoundList(sounds))
        return;

    const int entity_count = S_LoopSoundEntityCount();
    for (i = 0; i < entity_count; i++) {
        if (!sounds[i])
            continue;

        sfx = S_SfxForHandle(cl.sound_precache[sounds[i]]);
        if (!sfx)
            continue;       // bad sound effect
        sc = sfx->cache;
        if (!sc)
            continue;

        ent = S_LoopSoundEntity(i);
        if (!ent)
            continue;

        ch = AL_FindLoopingSound(ent->number, sfx);
        if (ch) {
            if (!S_BindChannelEntity(ch)) {
                AL_StopChannel(ch);
                ch = nullptr;
            } else {
                ch->autoframe = s_framecount;
                ch->end = s_paintedtime + sc->length;
                ch->no_merge = false;
                continue;
            }
        }

        // allocate a channel
        ch = S_PickChannel(0, 0);
        if (!ch)
            continue;

        ch->autosound = true;   // remove next frame
        ch->autoframe = s_framecount;
        ch->no_merge = false;
        ch->sfx = sfx;
        ch->entnum = ent->number;
        if (!S_BindChannelEntity(ch)) {
            memset(ch, 0, sizeof(*ch));
            continue;
        }
        ch->master_vol = S_GetEntityLoopVolume(ent);
        ch->dist_mult = S_GetEntityLoopDistMult(ent);
        ch->end = s_paintedtime + sc->length;

        AL_PlayChannel(ch);
    }
}

#define MAX_STREAM_BUFFERS  32

static void AL_StreamUpdate(void)
{
    ALint num_buffers = 0;
    qalGetSourcei(s_stream, AL_BUFFERS_PROCESSED, &num_buffers);

    while (num_buffers > 0) {
        ALuint buffers[MAX_STREAM_BUFFERS];
        ALsizei n = min(num_buffers, static_cast<ALint>(q_countof(buffers)));
        Q_assert(s_stream_buffers >= n);

        qalSourceUnqueueBuffers(s_stream, n, buffers);
        qalDeleteBuffers(n, buffers);

        s_stream_buffers -= n;
        num_buffers -= n;
    }
}

static void AL_StreamStop(void)
{
    qalSourceStop(s_stream);
    AL_StreamUpdate();
    Q_assert(!s_stream_buffers);
    s_stream_paused = false;
}

static void AL_StreamPause(bool paused)
{
    s_stream_paused = paused;

    // force pause if not active
    if (!s_active)
        paused = true;

    ALint state = 0;
    qalGetSourcei(s_stream, AL_SOURCE_STATE, &state);

    if (paused && state == AL_PLAYING)
        qalSourcePause(s_stream);

    if (!paused && state != AL_PLAYING && s_stream_buffers)
        qalSourcePlay(s_stream);
}

static int AL_NeedRawSamples(void)
{
    return s_stream_buffers < MAX_STREAM_BUFFERS ? MAX_RAW_SAMPLES : 0;
}

static int AL_HaveRawSamples(void)
{
    return s_stream_buffers * MAX_RAW_SAMPLES;
}

static bool AL_RawSamples(int samples, int rate, int width, int channels, const void *data, float volume)
{
    ALenum format = AL_GetSampleFormat(width, channels);
    if (!format)
        return false;

    if (AL_NeedRawSamples()) {
        ALuint buffer = 0;

        qalGetError();
        qalGenBuffers(1, &buffer);
        if (qalGetError())
            return false;

        qalBufferData(buffer, format, data, samples * width * channels, rate);
        if (qalGetError()) {
            qalDeleteBuffers(1, &buffer);
            return false;
        }

        qalSourceQueueBuffers(s_stream, 1, &buffer);
        if (qalGetError()) {
            qalDeleteBuffers(1, &buffer);
            return false;
        }
        s_stream_buffers++;
    }

    qalSourcef(s_stream, AL_GAIN, volume);

    ALint state = AL_PLAYING;
    qalGetSourcei(s_stream, AL_SOURCE_STATE, &state);
    if (state != AL_PLAYING) {
        qalSourcePlay(s_stream);
        s_stream_paused = false;
    }
    return true;
}

static void AL_UpdateUnderWater(void)
{
    bool underwater = S_IsUnderWater();
    ALint filter = 0;

    if (!s_underwater_filter)
        return;

    if (s_underwater_flag == underwater)
        return;

    if (underwater)
        filter = s_underwater_filter;

    for (int i = 0; i < s_numchannels; i++)
        qalSourcei(s_srcnums[i], AL_DIRECT_FILTER, filter);

    s_underwater_flag = underwater;
}

static void AL_UpdateListenerVelocity(void)
{
    vec3_t velocity = { 0.0f, 0.0f, 0.0f };

    if (AL_DopplerEnabled() && cls.state == ca_active) {
        if (!cls.demo.playback && cl_predict->integer &&
            !(cl.frame.ps.pmove.pm_flags & PMF_NO_PREDICTION)) {
            VectorCopy(cl.predicted_velocity, velocity);
        } else {
            VectorCopy(cl.frame.ps.pmove.velocity, velocity);
        }
    }

    float min_speed = al_doppler_min_speed ? Cvar_ClampValue(al_doppler_min_speed, 0.0f, 20000.0f) : 0.0f;
    if (min_speed > 0.0f && VectorLength(velocity) < min_speed)
        VectorClear(velocity);

    float smooth = al_doppler_smooth ? Cvar_ClampValue(al_doppler_smooth, 0.0f, 40.0f) : 0.0f;
    if (smooth > 0.0f && cls.frametime > 0.0f) {
        float lerp = 1.0f - expf(-smooth * Q_clipf(cls.frametime, 0.0f, 0.1f));
        s_doppler_listener_velocity[0] = FASTLERP(s_doppler_listener_velocity[0], velocity[0], lerp);
        s_doppler_listener_velocity[1] = FASTLERP(s_doppler_listener_velocity[1], velocity[1], lerp);
        s_doppler_listener_velocity[2] = FASTLERP(s_doppler_listener_velocity[2], velocity[2], lerp);
    } else {
        VectorCopy(velocity, s_doppler_listener_velocity);
    }

    vec3_t al_velocity;
    AL_CopyVector(s_doppler_listener_velocity, al_velocity);
    qalListener3f(AL_VELOCITY, al_velocity[0], al_velocity[1], al_velocity[2]);
}

static void AL_Activate(void)
{
    S_StopAllSounds();
    AL_StreamPause(s_stream_paused);
}

static void AL_Update(void)
{
    int         i;
    channel_t   *ch;
    ALfloat     orientation[6];

    if (!s_active)
        return;

    // handle time wraparound. FIXME: get rid of this?
    i = cls.realtime & MASK(30);
    if (i < s_paintedtime)
        S_StopAllSounds();
    s_paintedtime = i;

    // set listener parameters
    qalListener3f(AL_POSITION, AL_UnpackVector(listener_origin));
    AL_CopyVector(listener_forward, orientation);
    AL_CopyVector(listener_up, orientation + 3);
    qalListenerfv(AL_ORIENTATION, orientation);
    AL_UpdateListenerVelocity();

    AL_UpdateUnderWater();

    if (al_reverb && al_reverb->integer) {
        if (!AL_UpdateEAXEnvironment())
            AL_UpdateReverb(s_reverb_driver != AL_REVERB_DRIVER_AUTO);
    }

    // update spatialization for dynamic sounds
    for (i = 0, ch = s_channels; i < s_numchannels; i++, ch++) {
        if (!ch->sfx)
            continue;

        if (ch->autosound) {
            // autosounds are regenerated fresh each frame
            if (ch->autoframe != s_framecount) {
                AL_StopChannel(ch);
                continue;
            }
        } else {
            ALenum state = AL_STOPPED;
            qalGetSourcei(ch->srcnum, AL_SOURCE_STATE, &state);
            if (state == AL_STOPPED) {
                AL_StopChannel(ch);
                continue;
            }
        }

#if USE_DEBUG
        if (s_show->integer) {
            ALfloat offset = 0;
            qalGetSourcef(ch->srcnum, AL_SEC_OFFSET, &offset);
            Com_Printf("%d %.1f %.1f %s\n", i, ch->master_vol, offset, ch->sfx->name);
        }
#endif

        AL_Spatialize(ch);  // respatialize channel
    }

    s_framecount++;

    // add loopsounds
    if (al_merge_looping->integer >= s_merge_looping_minval) {
        AL_MergeLoopSounds();
    } else {
        AL_AddLoopSounds();
    }

    AL_IssuePlaysounds();

    AL_StreamUpdate();
}

static void AL_EndRegistration(void)
{
    AL_LoadEAXEffectProfiles();
    AL_LoadEAXZones();

    if (cl.bsp && s_reverb_environments) {
        AL_BuildAcousticRegions();
        AL_SetReverbStepIDs();
        AL_LoadAcousticSidecar();
        AL_ResetReverbState();
    } else {
        AL_FreeAcousticRegions();
    }
}

const sndapi_t snd_openal = {
    .init = AL_Init,
    .shutdown = AL_Shutdown,
    .update = AL_Update,
    .activate = AL_Activate,
    .sound_info = AL_SoundInfo,
    .upload_sfx = AL_UploadSfx,
    .delete_sfx = AL_DeleteSfx,
    .set_eax_effect_properties = AL_SetEAXEffectProperties,
    .raw_samples = AL_RawSamples,
    .need_raw_samples = AL_NeedRawSamples,
    .have_raw_samples = AL_HaveRawSamples,
    .drop_raw_samples = AL_StreamStop,
    .pause_raw_samples = AL_StreamPause,
    .get_begin_ofs = AL_GetBeginofs,
    .play_channel = AL_PlayChannel,
    .stop_channel = AL_StopChannel,
    .stop_all_sounds = AL_StopAllSounds,
    .get_sample_rate = QAL_GetSampleRate,
    .end_registration = AL_EndRegistration,
};
