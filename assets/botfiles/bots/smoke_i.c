//===========================================================================
//
// Name:			smoke_i.c
// Function:		smoke item weights
// Source:			WORR original Q3-style BotLib seed
// Tab Size:		4 (real tabs)
//===========================================================================

#include "inv.h"

#define FS_HEALTH			1.00
#define FS_ARMOR			0.90

#define W_SHOTGUN			90
#define W_SUPER_SHOTGUN		110
#define W_MACHINEGUN		80
#define W_CHAINGUN			130
#define W_GRENADELAUNCHER	120
#define W_ROCKETLAUNCHER	300
#define W_HYPERBLASTER		160
#define W_RAILGUN			180
#define W_BFG10K			80

#define GWW_SHOTGUN			58
#define GWW_SUPER_SHOTGUN			72
#define GWW_MACHINEGUN			52
#define GWW_CHAINGUN			84
#define GWW_GRENADELAUNCHER			78
#define GWW_ROCKETLAUNCHER			195
#define GWW_HYPERBLASTER			104
#define GWW_RAILGUN			117
#define GWW_BFG10K			52
#define I_ARMOR_SHARD		71
#define W_ARMOR_SHARD		I_ARMOR_SHARD
#define W_POWER_SCREEN		56

#define I_BODY_ARMOR		100
#define I_COMBAT_ARMOR		95
#define I_HEALTH			100
#define I_AMMO_SHELLS		60
#define I_AMMO_BULLETS		50
#define I_AMMO_GRENADES		110
#define I_AMMO_CELLS		80
#define I_AMMO_ROCKETS		140
#define I_AMMO_SLUGS		90
#define I_AMMO_BFG			45
#define I_QUAD				120
#define I_INVULNERABILITY	60
#define I_POWER_SHIELD		70
#define I_ADRENALINE		105
#define I_ANCIENT_HEAD		75
#define I_BANDOLIER			70
#define I_AMMOPACK			95
#define I_SILENCER			35
#define I_REBREATHER		45
#define I_ENVIRO			45
#define I_COOP_KEY			85
#define I_FLAG				90

#define W_BODY_ARMOR		I_BODY_ARMOR
#define W_COMBAT_ARMOR		I_COMBAT_ARMOR
#define W_HEALTH			I_HEALTH
#define W_MEGAHEALTH		100
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
#define ROAM_WEIGHT			55

#include "fw_items.c"
