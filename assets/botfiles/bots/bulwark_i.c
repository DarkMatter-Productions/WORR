//===========================================================================
//
// Name:			bulwark_i.c
// Function:		bulwark item weights
// Source:			WORR original Q3-style BotLib seed
// Tab Size:		4 (real tabs)
//===========================================================================

#include "inv.h"

#define FS_HEALTH			0.95
#define FS_ARMOR			1.35

#define W_SHOTGUN			240
#define W_SUPER_SHOTGUN		210
#define W_MACHINEGUN		90
#define W_CHAINGUN			80
#define W_GRENADELAUNCHER	110
#define W_ROCKETLAUNCHER	120
#define W_HYPERBLASTER		100
#define W_RAILGUN			80
#define W_BFG10K			40

#define GWW_SHOTGUN			156
#define GWW_SUPER_SHOTGUN			136
#define GWW_MACHINEGUN			58
#define GWW_CHAINGUN			52
#define GWW_GRENADELAUNCHER			72
#define GWW_ROCKETLAUNCHER			78
#define GWW_HYPERBLASTER			65
#define GWW_RAILGUN			52
#define GWW_BFG10K			26
#define I_ARMOR_SHARD		90
#define W_ARMOR_SHARD		I_ARMOR_SHARD
#define W_POWER_SCREEN		104

#define I_BODY_ARMOR		140
#define I_COMBAT_ARMOR		120
#define I_HEALTH			110
#define I_AMMO_SHELLS		90
#define I_AMMO_BULLETS		55
#define I_AMMO_GRENADES		80
#define I_AMMO_CELLS		65
#define I_AMMO_ROCKETS		75
#define I_AMMO_SLUGS		50
#define I_AMMO_BFG			20
#define I_QUAD				40
#define I_INVULNERABILITY	90
#define I_POWER_SHIELD		130
#define I_ADRENALINE		65
#define I_ANCIENT_HEAD		55
#define I_BANDOLIER			75
#define I_AMMOPACK			70
#define I_SILENCER			25
#define I_REBREATHER		50
#define I_ENVIRO			50
#define I_COOP_KEY			120
#define I_FLAG				120

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
#define W_ADRENALINE		I_ADRENALINE
#define W_ANCIENT_HEAD		I_ANCIENT_HEAD
#define W_BANDOLIER			I_BANDOLIER
#define W_AMMOPACK			I_AMMOPACK
#define W_SILENCER			I_SILENCER
#define W_REBREATHER		I_REBREATHER
#define W_ENVIRO			I_ENVIRO
#define W_COOP_KEY			I_COOP_KEY
#define FLAG_WEIGHT			I_FLAG
#define ROAM_WEIGHT			30

#include "fw_items.c"
