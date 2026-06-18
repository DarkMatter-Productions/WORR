//===========================================================================
//
// Name:			fw_items.c
// Function:		shared WORR item fuzzy weights
// Source:			WORR original Quake II BotLib seed
// Tab Size:		4 (real tabs)
//===========================================================================

//#define WEAPONS_STAY

#ifndef FS_HEALTH
#define FS_HEALTH			1.0
#endif
#ifndef FS_ARMOR
#define FS_ARMOR			1.0
#endif

#define BR_ARMOR			30
#define BR_HEALTH			30
#define BR_WEAPON			30
#define BR_AMMO				30
#define BR_POWERUP			30
#define BR_ROAM				15

#define MZ(value)			((value) < 0 ? 0 : (value))
#define ARMOR_SCALE(v)		balance($evalfloat(MZ(FS_ARMOR * v)), $evalfloat(MZ(FS_ARMOR * v - BR_ARMOR)), $evalfloat(MZ(FS_ARMOR * v + BR_ARMOR)))
#define HEALTH_SCALE(v)		balance($evalfloat(MZ(FS_HEALTH * v)), $evalfloat(MZ(FS_HEALTH * v - BR_HEALTH)), $evalfloat(MZ(FS_HEALTH * v + BR_HEALTH)))
#define WEAPON_SCALE(v)		balance($evalfloat(MZ(v)), $evalfloat(MZ(v - BR_WEAPON)), $evalfloat(MZ(v + BR_WEAPON)))
#define AMMO_SCALE(v)		balance($evalfloat(MZ(v)), $evalfloat(MZ(v - BR_AMMO)), $evalfloat(MZ(v + BR_AMMO)))
#define POWERUP_SCALE(v)	balance($evalfloat(MZ(v)), $evalfloat(MZ(v - BR_POWERUP)), $evalfloat(MZ(v + BR_POWERUP)))
#define ROAM_SCALE(v)		balance($evalfloat(MZ(v)), $evalfloat(MZ(v - BR_ROAM)), $evalfloat(MZ(v + BR_ROAM)))

#ifndef W_ARMOR_SHARD
#define W_ARMOR_SHARD		40
#endif
#ifndef W_BODY_ARMOR
#define W_BODY_ARMOR		100
#endif
#ifndef W_COMBAT_ARMOR
#define W_COMBAT_ARMOR		90
#endif
#ifndef W_HEALTH
#define W_HEALTH			100
#endif
#ifndef W_MEGAHEALTH
#define W_MEGAHEALTH		100
#endif
#ifndef W_POWER_SCREEN
#define W_POWER_SCREEN		60
#endif
#ifndef W_POWER_SHIELD
#define W_POWER_SHIELD		70
#endif
#ifndef FLAG_WEIGHT
#define FLAG_WEIGHT			80
#endif
#ifndef W_FLAG
#define W_FLAG				FLAG_WEIGHT
#endif
#ifndef W_ROAM
#ifdef ROAM_WEIGHT
#define W_ROAM				ROAM_WEIGHT
#else
#define W_ROAM				20
#endif
#endif

#ifndef GWW_SHOTGUN
#define GWW_SHOTGUN			W_SHOTGUN
#endif
#ifndef GWW_SUPER_SHOTGUN
#define GWW_SUPER_SHOTGUN	W_SUPER_SHOTGUN
#endif
#ifndef GWW_MACHINEGUN
#define GWW_MACHINEGUN		W_MACHINEGUN
#endif
#ifndef GWW_CHAINGUN
#define GWW_CHAINGUN		W_CHAINGUN
#endif
#ifndef GWW_GRENADES
#define GWW_GRENADES		W_GRENADES
#endif
#ifndef GWW_GRENADELAUNCHER
#define GWW_GRENADELAUNCHER	W_GRENADELAUNCHER
#endif
#ifndef GWW_ROCKETLAUNCHER
#define GWW_ROCKETLAUNCHER	W_ROCKETLAUNCHER
#endif
#ifndef GWW_HYPERBLASTER
#define GWW_HYPERBLASTER	W_HYPERBLASTER
#endif
#ifndef GWW_RAILGUN
#define GWW_RAILGUN			W_RAILGUN
#endif
#ifndef GWW_BFG10K
#define GWW_BFG10K			W_BFG10K
#endif

weight "item_armor_shard"
{
	switch(INVENTORY_ARMOR)
	{
		case 200: return 0;
		case 150: return ARMOR_SCALE($evalint(W_ARMOR_SHARD * 0.35));
		case 100: return ARMOR_SCALE($evalint(W_ARMOR_SHARD * 0.65));
		default: return ARMOR_SCALE(W_ARMOR_SHARD);
	} //end switch
} //end weight

weight "item_armor_jacket"
{
	switch(INVENTORY_ARMOR)
	{
		case 200: return 0;
		case 150: return ARMOR_SCALE($evalint(W_COMBAT_ARMOR * 0.45));
		default: return ARMOR_SCALE($evalint(W_COMBAT_ARMOR * 0.75));
	} //end switch
} //end weight

weight "item_armor_combat"
{
	switch(INVENTORY_ARMOR)
	{
		case 200: return 0;
		case 150: return ARMOR_SCALE($evalint(W_COMBAT_ARMOR * 0.55));
		default: return ARMOR_SCALE(W_COMBAT_ARMOR);
	} //end switch
} //end weight

weight "item_armor_body"
{
	switch(INVENTORY_ARMOR)
	{
		case 200: return 0;
		case 150: return ARMOR_SCALE($evalint(W_BODY_ARMOR * 0.65));
		default: return ARMOR_SCALE(W_BODY_ARMOR);
	} //end switch
} //end weight

weight "item_health_small"
{
	switch(INVENTORY_HEALTH)
	{
		case 100: return 0;
		case 75: return HEALTH_SCALE($evalint(W_HEALTH * 0.40));
		case 50: return HEALTH_SCALE($evalint(W_HEALTH * 0.70));
		default: return HEALTH_SCALE($evalint(W_HEALTH * 0.85));
	} //end switch
} //end weight

weight "item_health"
{
	switch(INVENTORY_HEALTH)
	{
		case 100: return 0;
		case 75: return HEALTH_SCALE($evalint(W_HEALTH * 0.55));
		case 50: return HEALTH_SCALE($evalint(W_HEALTH * 0.85));
		default: return HEALTH_SCALE(W_HEALTH);
	} //end switch
} //end weight

weight "item_health_large"
{
	switch(INVENTORY_HEALTH)
	{
		case 100: return HEALTH_SCALE($evalint(W_HEALTH * 0.25));
		case 75: return HEALTH_SCALE($evalint(W_HEALTH * 0.70));
		default: return HEALTH_SCALE(W_HEALTH);
	} //end switch
} //end weight

weight "item_health_mega"
{
	return HEALTH_SCALE(W_MEGAHEALTH);
} //end weight

#ifdef WEAPONS_STAY
#define HELD_WEAPON_WEIGHT(stay_weight) WEAPON_SCALE(stay_weight)
#else
#define HELD_WEAPON_WEIGHT(stay_weight) 1
#endif

#define WEAPON_ITEM_WEIGHT(inventory, base_weight, stay_weight) \
	switch(inventory) \
	{ \
		case 1: return WEAPON_SCALE(base_weight); \
		default: \
		{ \
			return HELD_WEAPON_WEIGHT(stay_weight); \
		} \
	}

weight "weapon_shotgun"
{
	WEAPON_ITEM_WEIGHT(INVENTORY_SHOTGUN, W_SHOTGUN, GWW_SHOTGUN);
} //end weight

weight "weapon_supershotgun"
{
	WEAPON_ITEM_WEIGHT(INVENTORY_SUPER_SHOTGUN, W_SUPER_SHOTGUN, GWW_SUPER_SHOTGUN);
} //end weight

weight "weapon_machinegun"
{
	WEAPON_ITEM_WEIGHT(INVENTORY_MACHINEGUN, W_MACHINEGUN, GWW_MACHINEGUN);
} //end weight

weight "weapon_chaingun"
{
	WEAPON_ITEM_WEIGHT(INVENTORY_CHAINGUN, W_CHAINGUN, GWW_CHAINGUN);
} //end weight

weight "weapon_grenadelauncher"
{
	WEAPON_ITEM_WEIGHT(INVENTORY_GRENADELAUNCHER, W_GRENADELAUNCHER, GWW_GRENADELAUNCHER);
} //end weight

weight "weapon_rocketlauncher"
{
	WEAPON_ITEM_WEIGHT(INVENTORY_ROCKETLAUNCHER, W_ROCKETLAUNCHER, GWW_ROCKETLAUNCHER);
} //end weight

weight "weapon_hyperblaster"
{
	WEAPON_ITEM_WEIGHT(INVENTORY_HYPERBLASTER, W_HYPERBLASTER, GWW_HYPERBLASTER);
} //end weight

weight "weapon_railgun"
{
	WEAPON_ITEM_WEIGHT(INVENTORY_RAILGUN, W_RAILGUN, GWW_RAILGUN);
} //end weight

weight "weapon_bfg"
{
	WEAPON_ITEM_WEIGHT(INVENTORY_BFG10K, W_BFG10K, GWW_BFG10K);
} //end weight

#define AMMO_WEIGHT(inventory, weight) \
	switch(inventory) \
	{ \
		case 200: return 0; \
		case 100: return AMMO_SCALE($evalint(weight * 0.45)); \
		case 50: return AMMO_SCALE($evalint(weight * 0.75)); \
		default: return AMMO_SCALE(weight); \
	}

weight "ammo_shells"
{
	AMMO_WEIGHT(INVENTORY_SHELLS, W_SHELLS);
} //end weight

weight "ammo_bullets"
{
	AMMO_WEIGHT(INVENTORY_BULLETS, W_BULLETS);
} //end weight

weight "ammo_grenades"
{
	AMMO_WEIGHT(INVENTORY_GRENADES, W_GRENADES);
} //end weight

weight "ammo_cells"
{
	AMMO_WEIGHT(INVENTORY_CELLS, W_CELLS);
} //end weight

weight "ammo_rockets"
{
	AMMO_WEIGHT(INVENTORY_ROCKETS, W_ROCKETS);
} //end weight

weight "ammo_slugs"
{
	AMMO_WEIGHT(INVENTORY_SLUGS, W_SLUGS);
} //end weight

weight "ammo_bfg"
{
	AMMO_WEIGHT(INVENTORY_BFGAMMO, W_BFGAMMO);
} //end weight

weight "item_quad"
{
	return POWERUP_SCALE(W_QUAD);
} //end weight

weight "item_invulnerability"
{
	return POWERUP_SCALE(W_INVULNERABILITY);
} //end weight

weight "item_power_screen"
{
	return POWERUP_SCALE(W_POWER_SCREEN);
} //end weight

weight "item_power_shield"
{
	return POWERUP_SCALE(W_POWER_SHIELD);
} //end weight

weight "item_flag_team1"
{
	return W_FLAG;
} //end weight

weight "item_flag_team2"
{
	return W_FLAG;
} //end weight

weight "team_CTF_redflag"
{
	return W_FLAG;
} //end weight

weight "team_CTF_blueflag"
{
	return W_FLAG;
} //end weight

weight "item_botroam"
{
	return ROAM_SCALE(W_ROAM);
} //end weight
