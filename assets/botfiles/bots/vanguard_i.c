//===========================================================================
//
// Name:			vanguard_i.c
// Function:		vanguard item weights
// Source:			WORR original Q3-style BotLib seed
// Tab Size:		4 (real tabs)
//===========================================================================

#include "inv.h"

#define FS_HEALTH			0.85
#define FS_ARMOR			1.05

#define W_SHOTGUN			110
#define W_SUPER_SHOTGUN		150
#define W_MACHINEGUN		160
#define W_CHAINGUN			280
#define W_GRENADELAUNCHER	90
#define W_ROCKETLAUNCHER	220
#define W_HYPERBLASTER		170
#define W_RAILGUN			140
#define W_BFG10K			90

#define GWW_SHOTGUN			72
#define GWW_SUPER_SHOTGUN			98
#define GWW_MACHINEGUN			104
#define GWW_CHAINGUN			182
#define GWW_GRENADELAUNCHER			58
#define GWW_ROCKETLAUNCHER			143
#define GWW_HYPERBLASTER			110
#define GWW_RAILGUN			91
#define GWW_BFG10K			58
#define I_ARMOR_SHARD		75
#define W_ARMOR_SHARD		I_ARMOR_SHARD
#define W_POWER_SCREEN		56

#define I_BODY_ARMOR		80
#define I_COMBAT_ARMOR		100
#define I_HEALTH			85
#define I_AMMO_SHELLS		70
#define I_AMMO_BULLETS		140
#define I_AMMO_GRENADES		70
#define I_AMMO_CELLS		85
#define I_AMMO_ROCKETS		110
#define I_AMMO_SLUGS		75
#define I_AMMO_BFG			55
#define I_QUAD				150
#define I_INVULNERABILITY	80
#define I_POWER_SHIELD		70
#define I_FLAG				110

#define W_BODY_ARMOR		I_BODY_ARMOR
#define W_COMBAT_ARMOR		I_COMBAT_ARMOR
#define W_HEALTH			I_HEALTH
#define W_MEGAHEALTH		80
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
#define ROAM_WEIGHT			50

#include "fw_items.c"
