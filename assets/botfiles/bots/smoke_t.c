//===========================================================================
//
// Name:			smoke_t.c
// Function:		chat lines for smoke
// Source:			WORR original Q3-style BotLib seed
// Tab Size:		4 (real tabs)
//===========================================================================

chat "smoke"
{
	#include "teamplay.h"

	type "game_enter"
	{
		"Smoke in.";
		"Moving.";
	} //end type

	type "level_start"
	{
		"Fast route.";
		"Watch the rockets.";
	} //end type

	type "hit_nodeath"
	{
		"Seen.";
		"Close.";
	} //end type

	type "kill_insult"
	{
		"Too slow.";
		"Route belonged to me.";
	} //end type

	type "death_praise"
	{
		"Good shot.";
		"Fair hit.";
	} //end type

	type "random_misc"
	{
		"Keep moving.";
		"Low profile.";
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
		"Hot floor.";
		"Bad jump.";
	} //end type

	type "death_slime"
	{
		"Dirty route.";
		"Noted.";
	} //end type

	type "death_drown"
	{
		"Out of air.";
		"Too slow underwater.";
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

	type "kill_rocket"
	{
		"Rocket landed.";
		"Bounce read, ", 0, ".";
	} //end type

	type "random_insult"
	{
		"You are making this route easy.";
		WORR_TAUNT0;
	} //end type

} //end chat smoke
