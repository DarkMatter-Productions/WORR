//===========================================================================
//
// Name:			fw_weap.c
// Function:		shared WORR weapon fuzzy weights
// Source:			WORR original Quake II BotLib seed
// Tab Size:		4 (real tabs)
//===========================================================================

#ifndef W_BLASTER
#define W_BLASTER			10
#endif
#ifndef W_SHOTGUN
#define W_SHOTGUN			100
#endif
#ifndef W_SUPER_SHOTGUN
#define W_SUPER_SHOTGUN		120
#endif
#ifndef W_MACHINEGUN
#define W_MACHINEGUN		100
#endif
#ifndef W_CHAINGUN
#define W_CHAINGUN			120
#endif
#ifndef W_GRENADES
#define W_GRENADES			60
#endif
#ifndef W_GRENADELAUNCHER
#define W_GRENADELAUNCHER	100
#endif
#ifndef W_ROCKETLAUNCHER
#define W_ROCKETLAUNCHER	160
#endif
#ifndef W_HYPERBLASTER
#define W_HYPERBLASTER		120
#endif
#ifndef W_RAILGUN
#define W_RAILGUN			160
#endif
#ifndef W_BFG10K
#define W_BFG10K			80
#endif

#define HAS_AMMO(inventory, weight) \
	switch(inventory) \
	{ \
		case 0: return 0; \
		default: return weight; \
	}

weight "Blaster"
{
	return W_BLASTER;
} //end weight

weight "Shotgun"
{
	HAS_AMMO(INVENTORY_SHELLS, W_SHOTGUN);
} //end weight

weight "Super Shotgun"
{
	switch(INVENTORY_SHELLS)
	{
		case 0: return 0;
		case 1: return W_SHOTGUN;
		default: return W_SUPER_SHOTGUN;
	} //end switch
} //end weight

weight "Machinegun"
{
	HAS_AMMO(INVENTORY_BULLETS, W_MACHINEGUN);
} //end weight

weight "Chaingun"
{
	switch(INVENTORY_BULLETS)
	{
		case 0: return 0;
		case 20: return $evalint(W_CHAINGUN * 0.60);
		default: return W_CHAINGUN;
	} //end switch
} //end weight

weight "Grenades"
{
	HAS_AMMO(INVENTORY_GRENADES, W_GRENADES);
} //end weight

weight "Grenade Launcher"
{
	HAS_AMMO(INVENTORY_GRENADES, W_GRENADELAUNCHER);
} //end weight

weight "Rocket Launcher"
{
	switch(INVENTORY_ROCKETS)
	{
		case 0: return 0;
		default:
		{
			switch(ENEMY_HORIZONTAL_DIST)
			{
				case 128: return $evalint(W_ROCKETLAUNCHER * 0.35);
				default: return W_ROCKETLAUNCHER;
			} //end switch
		} //end default
	} //end switch
} //end weight

weight "HyperBlaster"
{
	HAS_AMMO(INVENTORY_CELLS, W_HYPERBLASTER);
} //end weight

weight "Railgun"
{
	switch(INVENTORY_SLUGS)
	{
		case 0: return 0;
		default:
		{
			switch(NUM_VISIBLE_ENEMIES)
			{
				case 0: return $evalint(W_RAILGUN * 0.50);
				default: return W_RAILGUN;
			} //end switch
		} //end default
	} //end switch
} //end weight

weight "BFG10K"
{
	switch(INVENTORY_BFGAMMO)
	{
		case 0: return 0;
		default:
		{
			switch(NUM_VISIBLE_ENEMIES)
			{
				case 0: return $evalint(W_BFG10K * 0.25);
				case 1: return W_BFG10K;
				default: return $evalint(W_BFG10K * 1.35);
			} //end switch
		} //end default
	} //end switch
} //end weight
