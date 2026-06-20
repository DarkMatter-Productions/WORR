//===========================================================================
//
// Name:			relay_i.c
// Function:		relay item weights
// Source:			WORR original Q3-style BotLib seed
// Tab Size:		4 (real tabs)
//===========================================================================

#include "inv.h"

#define FS_HEALTH			1.20
#define FS_ARMOR			1.00

#define W_SHOTGUN			90
#define W_SUPER_SHOTGUN		100
#define W_MACHINEGUN		120
#define W_CHAINGUN			150
#define W_GRENADELAUNCHER	70
#define W_ROCKETLAUNCHER	130
#define W_HYPERBLASTER		260
#define W_RAILGUN			130
#define W_BFG10K			60

#define GWW_SHOTGUN			58
#define GWW_SUPER_SHOTGUN			65
#define GWW_MACHINEGUN			78
#define GWW_CHAINGUN			98
#define GWW_GRENADELAUNCHER			46
#define GWW_ROCKETLAUNCHER			84
#define GWW_HYPERBLASTER			169
#define GWW_RAILGUN			84
#define GWW_BFG10K			39
#define I_ARMOR_SHARD		79
#define W_ARMOR_SHARD		I_ARMOR_SHARD
#define W_POWER_SCREEN		68

#define I_BODY_ARMOR		95
#define I_COMBAT_ARMOR		105
#define I_HEALTH			125
#define I_AMMO_SHELLS		70
#define I_AMMO_BULLETS		90
#define I_AMMO_GRENADES		55
#define I_AMMO_CELLS		120
#define I_AMMO_ROCKETS		85
#define I_AMMO_SLUGS		80
#define I_AMMO_BFG			35
#define I_QUAD				80
#define I_INVULNERABILITY	70
#define I_POWER_SHIELD		85
#define I_ADRENALINE		80
#define I_ANCIENT_HEAD		70
#define I_BANDOLIER			115
#define I_AMMOPACK			130
#define I_SILENCER			45
#define I_REBREATHER		75
#define I_ENVIRO			75
#define I_COOP_KEY			125
#define I_FLAG				100

#define W_BODY_ARMOR		I_BODY_ARMOR
#define W_COMBAT_ARMOR		I_COMBAT_ARMOR
#define W_HEALTH			I_HEALTH
#define W_MEGAHEALTH		110
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
#define W_ADRENALINE		I_ADRENALINE
#define W_ANCIENT_HEAD		I_ANCIENT_HEAD
#define W_BANDOLIER			I_BANDOLIER
#define W_AMMOPACK			I_AMMOPACK
#define W_SILENCER			I_SILENCER
#define W_REBREATHER		I_REBREATHER
#define W_ENVIRO			I_ENVIRO
#define W_COOP_KEY			I_COOP_KEY
#define FLAG_WEIGHT			I_FLAG
#define ROAM_WEIGHT			45

#include "fw_items.c"
