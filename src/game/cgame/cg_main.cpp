// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "cg_local.h"
#include "cg_local_interaction.hpp"
#include "cg_wheel.h"
#include "cg_event_shadow.hpp"
#include "cg_event_runtime.hpp"
#include "cg_canonical_snapshot_timeline.hpp"
#include "client/cgame_entity_ext.h"
#include "client/cgame_ui_ext.h"
#include "shared/cgame_prediction.h"
#include "shared/prediction_abi.h"
#include "m_flash.hpp"

static_assert(static_cast<uint32_t>(MZ2_LAST) ==
              WORR_EVENT_MONSTER_MUZZLE_LAST);
static_assert(q_countof(monster_flash_offset) - 1 ==
              WORR_EVENT_MONSTER_MUZZLE_LAST);

cgame_import_t cgi;
cgame_export_t cglobals;

extern "C" void CG_UI_SetImport(const cgame_ui_import_t *import);
extern "C" const cgame_ui_export_t *CG_GetUIAPI(void);
struct cgame_entity_import_s;
struct cgame_entity_export_s;
using cgame_entity_import_t = cgame_entity_import_s;
using cgame_entity_export_t = cgame_entity_export_s;
extern "C" const cgame_entity_export_t *CG_GetEntityAPI(void);
void CG_Entity_SetImport(const cgame_entity_import_t *import);
extern "C" void CG_PredictionInputSetImport(
	const worr_cgame_prediction_input_import_v1 *import);
extern "C" void CG_PredictionInputSetImportV2(
	const worr_cgame_prediction_input_import_v2 *import);

static void *CG_GetExtension(const char *name)
{
	if (!name)
		return nullptr;

	if (!strcmp(name, CGAME_UI_EXPORT_EXT))
		return (void *)CG_GetUIAPI();
	if (!strcmp(name, CGAME_ENTITY_EXPORT_EXT))
		return (void *)CG_GetEntityAPI();
	if (!strcmp(name, WORR_CGAME_EVENT_SHADOW_EXPORT_V1))
		return (void *)CG_GetEventShadowAPI();
	if (!strcmp(name, WORR_CGAME_EVENT_RANGE_EXPORT_V2))
		return (void *)CG_GetEventRangeAPIv2();
	if (!strcmp(name, WORR_CGAME_EVENT_RUNTIME_EXPORT_V1))
		return (void *)CG_GetEventRuntimeAPI();
	if (!strcmp(name, WORR_CGAME_SNAPSHOT_TIMELINE_EXPORT_V2))
		return (void *)CG_GetCanonicalSnapshotTimelineAPI();

	return nullptr;
}

void CG_InitScreen();

uint64_t cgame_init_time = 0;

extern "C" void CG_GetPredictionConfigV1(worr_prediction_config_v1 *config)
{
	if (!config)
		return;

	*config = {};
	config->struct_size = sizeof(*config);
	config->schema_version = WORR_PREDICTION_ABI_VERSION;
	config->movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
	config->air_acceleration = pm_config.airAccel;
	if (pm_config.n64Physics)
		config->flags |= WORR_PREDICTION_CONFIG_N64_PHYSICS;
	if (pm_config.q3Overbounce)
		config->flags |= WORR_PREDICTION_CONFIG_Q3_OVERBOUNCE;
}

static void InitCGame()
{
	(void)CG_CanonicalSnapshotTimelineInitialize();
	CG_InitScreen();
	CG_Wheel_Init();
	CG_WeaponBar_Init();

	cgame_init_time = cgi.CL_ClientRealTime();

	pm_config.n64Physics = !!atoi(cgi.get_configString(CONFIG_N64_PHYSICS));
	pm_config.q3Overbounce = !!atoi(cgi.get_configString(CONFIG_Q3_OVERBOUNCE));
	pm_config.airAccel = atoi(cgi.get_configString(CS_AIRACCEL));
}

static void ShutdownCGame()
{
	CG_PredictionInputSetImport(nullptr);
	CG_PredictionInputSetImportV2(nullptr);
	CG_LocalInteractionSetImport(nullptr);
}

void CG_DrawHUD (int32_t isplit, const cg_server_data_t *data, vrect_t hud_vrect, vrect_t hud_safe, int32_t scale, int32_t playernum, const player_state_t *ps);
void CG_TouchPics();
layout_flags_t CG_LayoutFlags(const player_state_t *ps);
void CG_DrawChatHUD(int32_t isplit, vrect_t hud_vrect, vrect_t hud_safe, int32_t scale);
void CG_ChatHud_AddLine(int32_t isplit, const char *text, bool is_chat);
void CG_ChatHud_Clear(int32_t isplit);
void CG_ChatHud_ScrollLines(float delta);
void CG_ChatHud_MouseEvent(int x, int y);
void CG_ChatHud_MouseDown(int button);
void CG_DrawCrosshair(int32_t isplit, const player_state_t *ps);
void CG_NotifyPickupPulse(int32_t isplit);
void CG_NotifyHitMarker(int32_t isplit, int damage);
void CG_AddDamageDisplay(int32_t isplit, int damage, const Vector3 &color, const Vector3 &dir);
void CG_AddPOI(int32_t isplit, int id, int time, const Vector3 &pos, int image, int color, int flags);
void CG_RemovePOI(int32_t isplit, int id);
void CG_Hud_ParseConfigString(int32_t i, const char *s);

int32_t CG_GetActiveWeaponWheelWeapon(const player_state_t *ps)
{
	return ps->stats[STAT_ACTIVE_WHEEL_WEAPON];
}

uint32_t CG_GetOwnedWeaponWheelWeapons(const player_state_t *ps)
{
	return ((uint32_t) (uint16_t) ps->stats[STAT_WEAPONS_OWNED_1]) | ((uint32_t) (uint16_t) (ps->stats[STAT_WEAPONS_OWNED_2]) << 16);
}

int16_t CG_GetWeaponWheelAmmoCount(const player_state_t *ps, int32_t ammo_id)
{
    uint16_t ammo = GetAmmoStat((uint16_t *) &ps->stats[STAT_AMMO_INFO_START], ammo_id);

	if (ammo == AMMO_VALUE_INFINITE)
		return -1;

	return ammo;
}

int16_t CG_GetPowerupWheelCount(const player_state_t *ps, int32_t powerup_id)
{
    return GetPowerupStat((uint16_t *) &ps->stats[STAT_POWERUP_INFO_START], powerup_id);
}

int16_t CG_GetHitMarkerDamage(const player_state_t *ps)
{
	return ps->stats[STAT_HIT_MARKER];
}

static void CG_ParseConfigString(int32_t i, const char *s)
{
	if (i == CONFIG_N64_PHYSICS)
		pm_config.n64Physics = !!atoi(s);
	else if (i == CONFIG_Q3_OVERBOUNCE)
		pm_config.q3Overbounce = !!atoi(s);
	else if (i == CS_AIRACCEL)
		pm_config.airAccel = atoi(s);

	CG_Wheel_ParseConfigString(i, s);
	CG_Hud_ParseConfigString(i, s);
}

void CG_ParseCenterPrint (const char *str, int isplit, bool instant);
void CG_ClearNotify(int32_t isplit);
void CG_ClearCenterprint(int32_t isplit);
void CG_NotifyMessage(int32_t isplit, const char *msg, bool is_chat);

void CG_GetMonsterFlashOffset(MonsterMuzzleFlashID id, gvec3_ref_t offset)
{
	if (id >= q_countof(monster_flash_offset))
		cgi.Com_Error("Bad muzzle flash offset");

	offset = monster_flash_offset[id];
}

/*
=================
GetCGameAPI

Returns a pointer to the structure with all entry points
and global variables
=================
*/
Q2GAME_API cgame_export_t *GetCGameAPI(cgame_import_t *import)
{
	cgi = *import;
	if (import->GetExtension) {
		const cgame_ui_import_t *ui_import =
			(const cgame_ui_import_t *)import->GetExtension(CGAME_UI_IMPORT_EXT);
		CG_UI_SetImport(ui_import);
		const cgame_entity_import_t *entity_import =
			(const cgame_entity_import_t *)import->GetExtension(CGAME_ENTITY_IMPORT_EXT);
		CG_Entity_SetImport(entity_import);

		const worr_cgame_prediction_input_import_v1 *prediction_import =
			(const worr_cgame_prediction_input_import_v1 *)import->GetExtension(
				WORR_CGAME_PREDICTION_INPUT_IMPORT_V1);
		if (prediction_import &&
			(prediction_import->struct_size != sizeof(*prediction_import) ||
			 prediction_import->api_version !=
				 WORR_CGAME_PREDICTION_INPUT_API_VERSION ||
			 !prediction_import->ResolveInputRange)) {
			prediction_import = nullptr;
		}
		CG_PredictionInputSetImport(prediction_import);
		const worr_cgame_prediction_input_import_v2 *prediction_import_v2 =
			(const worr_cgame_prediction_input_import_v2 *)import->GetExtension(
				WORR_CGAME_PREDICTION_INPUT_IMPORT_V2);
		if (prediction_import_v2 &&
			(prediction_import_v2->struct_size != sizeof(*prediction_import_v2) ||
			 prediction_import_v2->api_version !=
				 WORR_CGAME_PREDICTION_INPUT_API_VERSION_V2 ||
			 !prediction_import_v2->ResolveInputRangeForCursor)) {
			prediction_import_v2 = nullptr;
		}
		CG_PredictionInputSetImportV2(prediction_import_v2);
		const worr_cgame_command_record_import_v1 *command_record_import =
			(const worr_cgame_command_record_import_v1 *)import->GetExtension(
				WORR_CGAME_COMMAND_RECORD_IMPORT_V1);
		if (command_record_import &&
			(command_record_import->struct_size != sizeof(*command_record_import) ||
			 command_record_import->api_version !=
				 WORR_CGAME_COMMAND_RECORD_API_VERSION ||
			 !command_record_import->ResolveCanonicalCommandRange)) {
			command_record_import = nullptr;
		}
		CG_LocalInteractionSetImport(command_record_import);
	} else {
		CG_PredictionInputSetImport(nullptr);
		CG_PredictionInputSetImportV2(nullptr);
		CG_LocalInteractionSetImport(nullptr);
	}

	cglobals.apiVersion = CGAME_API_VERSION;
	cglobals.Init = InitCGame;
	cglobals.Shutdown = ShutdownCGame;

	cglobals.DrawHUD = CG_DrawHUD;
	cglobals.LayoutFlags = CG_LayoutFlags;
	cglobals.TouchPics = CG_TouchPics;
	
	cglobals.GetActiveWeaponWheelWeapon = CG_GetActiveWeaponWheelWeapon;
	cglobals.GetOwnedWeaponWheelWeapons = CG_GetOwnedWeaponWheelWeapons;
	cglobals.GetWeaponWheelAmmoCount = CG_GetWeaponWheelAmmoCount;
	cglobals.GetPowerupWheelCount = CG_GetPowerupWheelCount;
	cglobals.GetHitMarkerDamage = CG_GetHitMarkerDamage;
	cglobals.ParseConfigString = CG_ParseConfigString;
	cglobals.ParseCenterPrint = CG_ParseCenterPrint;
	cglobals.ClearNotify = CG_ClearNotify;
	cglobals.ClearCenterprint = CG_ClearCenterprint;
	cglobals.NotifyMessage = CG_NotifyMessage;
	cglobals.GetMonsterFlashOffset = CG_GetMonsterFlashOffset;

	cglobals.Wheel_Open = CG_Wheel_Open;
	cglobals.Wheel_Close = CG_Wheel_Close;
	cglobals.Wheel_Input = CG_Wheel_Input;
	cglobals.Wheel_WeapNext = CG_Wheel_WeapNext;
	cglobals.Wheel_WeapPrev = CG_Wheel_WeapPrev;
	cglobals.Wheel_ApplyButtons = CG_Wheel_ApplyButtons;
	cglobals.Wheel_ClearInput = CG_Wheel_ClearInput;
	cglobals.Wheel_IsOpen = CG_Wheel_IsOpen;
	cglobals.Wheel_TimeScale = CG_Wheel_TimeScale;
	cglobals.Wheel_AllowAttack = CG_Wheel_AllowAttack;
	cglobals.Wheel_Update = CG_Wheel_Update;
	cglobals.WeaponBar_Input = CG_WeaponBar_Input;
	cglobals.WeaponBar_ClearInput = CG_WeaponBar_ClearInput;
	cglobals.DrawChatHUD = CG_DrawChatHUD;
	cglobals.ChatHud_AddLine = CG_ChatHud_AddLine;
	cglobals.ChatHud_Clear = CG_ChatHud_Clear;
	cglobals.ChatHud_ScrollLines = CG_ChatHud_ScrollLines;
	cglobals.ChatHud_MouseEvent = CG_ChatHud_MouseEvent;
	cglobals.ChatHud_MouseDown = CG_ChatHud_MouseDown;
	cglobals.DrawCrosshair = CG_DrawCrosshair;
	cglobals.NotifyPickupPulse = CG_NotifyPickupPulse;
	cglobals.NotifyHitMarker = CG_NotifyHitMarker;
	cglobals.AddDamageDisplay = CG_AddDamageDisplay;
	cglobals.AddPOI = CG_AddPOI;
	cglobals.RemovePOI = CG_RemovePOI;

	cglobals.GetExtension = CG_GetExtension;

	return &cglobals;
}
