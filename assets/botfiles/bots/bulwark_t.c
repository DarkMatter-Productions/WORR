//===========================================================================
//
// Name:			bulwark_t.c
// Function:		chat lines for bulwark
// Source:			WORR original Q3-style BotLib seed
// Tab Size:		4 (real tabs)
//===========================================================================

chat "bulwark"
{
	#include "teamplay.h"

	type "game_enter"
	{
		"Blue lane covered.";
		"Hold the line and call targets.";
	} //end type

	type "level_start"
	{
		"Anchor set.";
		"Let them spend ammo getting through me.";
	} //end type

	type "hit_nodeath"
	{
		"That angle is closed.";
		"Try the other corridor.";
	} //end type

	type "kill_insult"
	{
		"Route denied.";
		"That lane is closed.";
	} //end type

	type "death_insult"
	{
		"Enjoy the gap while it lasts.";
		"Resetting the wall.";
	} //end type

	type "random_misc"
	{
		"Armor timing is stable.";
		"Fallback route is clear.";
	} //end type
	type "game_exit"
	{
		"Rotating out.";
		WORR_GOODBYE0;
	} //end type

	type "level_end"
	{
		"Round closed.";
		"Check the scoreboard.";
	} //end type

	type "level_end_victory"
	{
		"Better routes, better timing.";
		WORR_VICTORY0;
	} //end type

	type "level_end_lose"
	{
		"Run it back.";
		WORR_LOSS0;
	} //end type

	type "hit_talking"
	{
		"Save the shot for the fight.";
		"I was still typing, ", 0, ".";
	} //end type

	type "damaged_nokill"
	{
		"Good hit.";
		"I felt that one.";
	} //end type

	type "hit_nokill"
	{
		"Tagged.";
		"That connected.";
	} //end type

	type "enemy_suicide"
	{
		"That was your decision, ", 0, ".";
		WORR_SELFOWN0;
	} //end type

	type "death_lava"
	{
		"Bad step near heat.";
		"Marking that floor hazard.";
	} //end type

	type "death_slime"
	{
		"Wrong liquid route.";
		"Next path avoids the pit.";
	} //end type

	type "death_drown"
	{
		"Air route failed.";
		"Need a cleaner water exit.";
	} //end type

	type "death_suicide"
	{
		"Wrong route.";
		"Bad timing on my part.";
	} //end type

	type "death_rail"
	{
		"Clean rail.";
		"Nice line, ", 0, ".";
	} //end type

	type "death_bfg"
	{
		"Hard to argue with that.";
		"Big shot, ", 0, ".";
	} //end type

	type "kill_shotgun"
	{
		"Corner held.";
		"Close route closed, ", 0, ".";
	} //end type

	type "kill_praise"
	{
		"Good pressure.";
		"Clean push.";
	} //end type

	type "random_insult"
	{
		"You are making this route easy.";
		WORR_TAUNT0;
	} //end type

} //end chat bulwark
