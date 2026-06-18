//===========================================================================
//
// Name:			vector_s.c
// Function:		duel route script for Vector
// Source:			WORR original Q3-style BotLib seed
// Scripter:		Worker R
// Last update:		2026-06-18
// Tab Size:		4 (real tabs)
//===========================================================================

script "main"
{
	//
	point("vector lower spawn", -64, -128, 32);
	point("vector rail shelf", -32, -224, 40);
	point("vector long sightline", 64, -256, 48);
	point("vector exit step", 112, -176, 32);
	//
	box("vector lower spawn box", -18, -18, -24, 18, 18, 44);
	box("vector rail shelf box", -18, -18, -24, 18, 18, 44);
	box("vector long sightline box", -20, -20, -24, 20, 20, 48);
	box("vector exit step box", -18, -18, -24, 18, 18, 44);
	//
	movebox("vector lower spawn box", "vector lower spawn");
	movebox("vector rail shelf box", "vector rail shelf");
	movebox("vector long sightline box", "vector long sightline");
	movebox("vector exit step box", "vector exit step");
	//
	say("Vector taking rail lane.", NULL);
	selectweapon(16);
	moveto("vector lower spawn box");
	wait(touch(0, "vector lower spawn box"));
	aim("vector rail shelf");
	wait(time(0.20));
	moveto("vector rail shelf box");
	wait(touch(0, "vector rail shelf box"));
	aim("vector long sightline");
	fireweapon();
	wait(time(0.35));
	say("Cross the line again.", NULL);
	moveto("vector long sightline box");
	wait(touch(0, "vector long sightline box"));
	aim("vector exit step");
	moveto("vector exit step box");
	wait(touch(0, "vector exit step box"));
} //end script main
