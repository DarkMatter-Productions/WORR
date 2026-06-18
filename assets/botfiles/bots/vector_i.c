//===========================================================================
//
// Name:			vector_i.c
// Function:		vector item weights
// Source:			WORR original Q3-style BotLib seed
// Tab Size:		4 (real tabs)
//===========================================================================

#include "inv.h"

#define FS_HEALTH			0.95
#define FS_ARMOR			1.00

#define W_SHOTGUN			100
#define W_SUPER_SHOTGUN		120
#define W_MACHINEGUN		90
#define W_CHAINGUN			120
#define W_GRENADELAUNCHER	80
#define W_ROCKETLAUNCHER	200
#define W_HYPERBLASTER		120
#define W_RAILGUN			300
#define W_BFG10K			75

#define GWW_SHOTGUN			65
#define GWW_SUPER_SHOTGUN			78
#define GWW_MACHINEGUN			58
#define GWW_CHAINGUN			78
#define GWW_GRENADELAUNCHER			52
#define GWW_ROCKETLAUNCHER			130
#define GWW_HYPERBLASTER			78
#define GWW_RAILGUN			195
#define GWW_BFG10K			49
#define I_ARMOR_SHARD		79
#define W_ARMOR_SHARD		I_ARMOR_SHARD
#define W_POWER_SCREEN		56

#define I_BODY_ARMOR		90
#define I_COMBAT_ARMOR		105
#define I_HEALTH			95
#define I_AMMO_SHELLS		65
#define I_AMMO_BULLETS		55
#define I_AMMO_GRENADES		70
#define I_AMMO_CELLS		70
#define I_AMMO_ROCKETS		100
#define I_AMMO_SLUGS		150
#define I_AMMO_BFG			45
#define I_QUAD				115
#define I_INVULNERABILITY	75
#define I_POWER_SHIELD		70
#define I_FLAG				80

#define W_BODY_ARMOR		I_BODY_ARMOR
#define W_COMBAT_ARMOR		I_COMBAT_ARMOR
#define W_HEALTH			I_HEALTH
#define W_MEGAHEALTH		95
#define W_SHELLS			I_AMMO_SHELLS
#define W_BULLETS			I_AMMO_BULLETS
#define W_GRENADES			I_AMMO_GRENADES
#define W_CELLS				I_AMMO_CELLS
#define W_ROCKETS			I_AMMO_ROCKETS
#define W_SLUGS				I_AMMO_SLUGS
#define W_BFGAMMO			I_AMMO_BFG
#define W_QUAD				I_QUAD
#define W_INVULNERABILITY	I_INVULNERABILITY
#define W_POWER_SHIELD		I_POWER_SHIELD
#define FLAG_WEIGHT			I_FLAG
#define ROAM_WEIGHT			60

#include "fw_items.c"
