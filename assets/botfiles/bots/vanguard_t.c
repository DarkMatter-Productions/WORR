//===========================================================================
//
// Name:			vanguard_t.c
// Function:		chat lines for vanguard
// Source:			WORR original Q3-style BotLib seed
// Tab Size:		4 (real tabs)
//===========================================================================

chat "vanguard"
{
	#include "teamplay.h"

	type "game_enter"
	{
		"Vanguard pushing.";
		"Front line is mine.";
	} //end type

	type "level_start"
	{
		"Take space early.";
		"Pressure first, questions later.";
	} //end type

	type "hit_nodeath"
	{
		"Now you have my attention.";
		"That will not stop the push.";
	} //end type

	type "kill_insult"
	{
		"Move faster.";
		"Broken line.";
	} //end type

	type "death_insult"
	{
		"You bought seconds, not safety.";
		"Hold that ground if you can.";
	} //end type

	type "random_misc"
	{
		"Quad decides fights.";
		"Choke point is open.";
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
		"Pressure route overheated.";
		"Bad push through heat.";
	} //end type

	type "death_slime"
	{
		"Wrong pit.";
		"That route is too slow.";
	} //end type

	type "death_drown"
	{
		"Water stalled the push.";
		"Need air before pressure.";
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

	type "kill_chaingun"
	{
		"Pressure paid.";
		"Line broken, ", 0, ".";
	} //end type

	type "random_insult"
	{
		"You are making this route easy.";
		WORR_TAUNT0;
	} //end type

} //end chat vanguard
