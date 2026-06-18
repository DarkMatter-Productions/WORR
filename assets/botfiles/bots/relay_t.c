//===========================================================================
//
// Name:			relay_t.c
// Function:		chat lines for relay
// Source:			WORR original Q3-style BotLib seed
// Tab Size:		4 (real tabs)
//===========================================================================

chat "relay"
{
	#include "teamplay.h"

	type "game_enter"
	{
		"Relay online.";
		"Call for cover if you rotate.";
	} //end type

	type "level_start"
	{
		"Tracking items and exits.";
		"Routes are live.";
	} //end type

	type "hit_nodeath"
	{
		"Contact on my route.";
		"I saw that angle.";
	} //end type

	type "kill_praise"
	{
		"Nice finish.";
		"Good trade.";
	} //end type

	type "death_praise"
	{
		"Well played.";
		"Clean read.";
	} //end type

	type "random_misc"
	{
		"Cells are worth checking.";
		"Next rotation is open.";
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

	type "kill_praise"
	{
		"Nice dodge. Still counted.";
		"Good movement, ", 0, ".";
	} //end type

	type "random_insult"
	{
		"You are making this route easy.";
		WORR_TAUNT0;
	} //end type

} //end chat relay
