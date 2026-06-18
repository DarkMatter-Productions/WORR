//===========================================================================
//
// Name:			vector_t.c
// Function:		chat lines for vector
// Source:			WORR original Q3-style BotLib seed
// Tab Size:		4 (real tabs)
//===========================================================================

chat "vector"
{
	#include "teamplay.h"

	type "game_enter"
	{
		"Vector locked.";
		"Pick a lane.";
	} //end type

	type "level_start"
	{
		"Angles first.";
		"Rail line is open.";
	} //end type

	type "hit_nodeath"
	{
		"Not enough lead.";
		"Adjust your aim.";
	} //end type

	type "kill_insult"
	{
		"Straight line, straight loss.";
		"Predicted.";
	} //end type

	type "death_praise"
	{
		"Fine shot.";
		"Good angle.";
	} //end type

	type "random_misc"
	{
		"Keep crossing that sightline.";
		"Slugs first.";
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

} //end chat vector
